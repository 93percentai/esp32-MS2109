#include "capture_config.h"
#include "display.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "bsp/esp-bsp.h"
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_private/esp_cache_private.h"
#include "esp_cache.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "video_store.h"
#include "wifi_mgr.h"
#include "provisioning.h"
#include "ppa_fit.h"

#if !CONFIG_LV_USE_PPA || !CONFIG_LV_USE_PPA_IMG || CONFIG_LV_DRAW_BUF_ALIGN != CONFIG_CACHE_L1_DCACHE_LINE_SIZE
#error "Enable LV_USE_PPA, LV_USE_PPA_IMG and LV_DRAW_BUF_ALIGN=64 in sdkconfig for native S31 rendering."
#endif
#define LCD_W 800
#define LCD_H 480
static lv_obj_t *s_image, *s_debug, *s_message;
static lv_image_dsc_t s_descriptor[2];
static uint16_t *s_pixels[2];
static unsigned s_front;
static int64_t s_display_us;
static bool s_debug_visible;
static uint8_t *s_input, *s_decoded;
static size_t s_input_capacity, s_decode_capacity;
static jpeg_decoder_handle_t s_decoder;
static ppa_client_handle_t s_scaler;
static TaskHandle_t s_display_task;
static const char *TAG = "display";
static const char *s_render_stage;
static esp_err_t render_jpeg(size_t size, const video_status_t *frame);
static void display_task(void *arg);
void display_notify(void) { if (s_display_task) xTaskNotifyGive(s_display_task); }

static void touch(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED) {
        network_request_setup();
    } else if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        s_debug_visible = !s_debug_visible;
        if (s_debug_visible) lv_obj_remove_flag(s_debug, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_debug, LV_OBJ_FLAG_HIDDEN);
    }
}

static void status_timer(lv_timer_t *timer)
{
    video_status_t video;
    video_status(&video);
    provisioning_lcd_status_t setup;
    provisioning_get_lcd_status(&setup);
    char ip[32];
    wifi_mgr_get_ip(ip, sizeof(ip));
    bool fresh = video.connected && s_display_us && esp_timer_get_time() - s_display_us < 3000000;
    if (fresh) lv_obj_remove_flag(s_image, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_image, LV_OBJ_FLAG_HIDDEN);
    if (setup.active) {
        lv_label_set_text_fmt(s_message, "Wi-Fi setup: %s\nJoin " PROV_SOFTAP_SSID
            "\nhttp://192.168.4.1\n%s", setup.detail,
            fresh ? "Tap image for details" : "Connect MS2109 to USB-A HS and provide HDMI input");
    } else {
        lv_label_set_text_fmt(s_message, "%s\nhttp://%s/\nTap: image details | Hold 2s: Wi-Fi setup",
            fresh ? "MS2109 live" : "Waiting for video: check USB-A HS and HDMI input", ip);
    }
    static uint32_t last_sequence, last_display;
    static int64_t last_us;
    int64_t now = esp_timer_get_time();
    float capture_fps = last_us ? (video.sequence - last_sequence) * 1000000.0f / (now - last_us) : 0;
    float lcd_fps = last_us ? (video.display_frames - last_display) * 1000000.0f / (now - last_us) : 0;
    last_us = now; last_sequence = video.sequence; last_display = video.display_frames;
    lv_label_set_text_fmt(s_debug, "Capture %ux%u | %.1f fps\nLCD JPEG + PPA | %.1f fps\n"
        "HW JPEG %" PRIu32 " us | PPA %" PRIu32 " us\nLVGL submit %" PRIu32 " us | LCD age %" PRIu32 " ms\n"
        "Capture drops %" PRIu32 " | LCD errors %" PRIu32 "\nPSRAM free %u KiB\nBrowser http://%s/",
        video.width, video.height, capture_fps, lcd_fps, video.jpeg_decode_us, video.scale_us,
        video.present_us, video.display_age_us / 1000, video.dropped, video.display_errors,
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), ip);
}

esp_err_t display_init(void)
{
    jpeg_decode_engine_cfg_t engine = {.timeout_ms = 200};
    esp_err_t err = jpeg_new_decoder_engine(&engine, &s_decoder);
    if (err != ESP_OK) return err;
    jpeg_decode_memory_alloc_cfg_t mem = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    s_input = jpeg_alloc_decoder_mem(VIDEO_MAX_BYTES, &mem, &s_input_capacity);
    mem.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    /* Hardware JPEG output is padded to MCU boundaries. Allocate worst case. */
    size_t padded_w = (CAPTURE_WIDTH + 15) & ~15;
    size_t padded_h = (CAPTURE_HEIGHT + 15) & ~15;
    s_decoded = jpeg_alloc_decoder_mem(padded_w * padded_h * 2, &mem, &s_decode_capacity);
    ppa_client_config_t ppa = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_64};
    err = ppa_register_client(&ppa, &s_scaler);
    if (err != ESP_OK) return err;
    size_t alignment = 0;
    err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &alignment);
    if (err != ESP_OK) return err;
    for (int i = 0; i < 2; ++i) {
        s_pixels[i] = heap_caps_aligned_calloc(alignment, LCD_W * LCD_H, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pixels[i]) return ESP_ERR_NO_MEM;
        /* Commit the black borders before PPA invalidates/writes output cache lines. */
        err = esp_cache_msync(s_pixels[i], LCD_W * LCD_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (err != ESP_OK) return err;
        s_descriptor[i] = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                       .w = LCD_W, .h = LCD_H, .stride = LCD_W * 2},
            .data_size = LCD_W * LCD_H * 2, .data = (uint8_t *)s_pixels[i]};
    }
    if (!s_input || !s_decoded) return ESP_ERR_NO_MEM;
    if (!bsp_display_start()) return ESP_FAIL;
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) lv_indev_set_long_press_time(indev, 2000);
    bsp_display_lock(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, touch, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(screen, touch, LV_EVENT_LONG_PRESSED, NULL);
    s_image = lv_image_create(screen);
    lv_image_set_src(s_image, &s_descriptor[0]);
    lv_obj_center(s_image);
    lv_obj_remove_flag(s_image, LV_OBJ_FLAG_CLICKABLE);
    s_message = lv_label_create(screen);
    lv_obj_set_style_text_color(s_message, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_message, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_message, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_message, 8, 0);
    lv_obj_align(s_message, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_remove_flag(s_message, LV_OBJ_FLAG_CLICKABLE);
    s_debug = lv_label_create(screen);
    lv_obj_set_style_text_color(s_debug, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_debug, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_debug, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(s_debug, 12, 0);
    lv_obj_align(s_debug, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(s_debug, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_debug, LV_OBJ_FLAG_CLICKABLE);
    lv_timer_create(status_timer, 1000, NULL);
    bsp_display_unlock();
    ESP_LOGI(TAG, "Native JPEG decode + PPA scale + PPA LVGL composition; independent latest-frame worker");
    return xTaskCreate(display_task, "lcd_video", 6144, NULL, 5, &s_display_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t render_jpeg(size_t size, const video_status_t *frame)
{
    s_render_stage = "JPEG header";
    unsigned width = frame->width, height = frame->height;
    jpeg_decode_picture_info_t info;
    esp_err_t err = jpeg_decoder_get_info(s_input, size, &info);
    if (err != ESP_OK) return err;
    if (info.width != width || info.height != height || !width || !height ||
        width > CAPTURE_WIDTH || height > CAPTURE_HEIGHT) return ESP_ERR_INVALID_SIZE;
    jpeg_decode_cfg_t config = {.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        /* RGB selects byte-swapped RGB565; LVGL needs native little-endian. */
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR, .conv_std = JPEG_YUV_RGB_CONV_STD_BT601};
    uint32_t output_size;
    s_render_stage = "JPEG DMA decode";
    int64_t start = esp_timer_get_time();
    err = jpeg_decoder_process(s_decoder, &config, s_input, size,
                               s_decoded, s_decode_capacity, &output_size);
    uint32_t jpeg_us = esp_timer_get_time() - start;
    if (err != ESP_OK) return err;
    s_render_stage = "DMA geometry";
    unsigned block = (info.sample_method == JPEG_DOWN_SAMPLING_YUV444 ||
                      info.sample_method == JPEG_DOWN_SAMPLING_GRAY) ? 8 : 16;
    unsigned stride = (width + block - 1) & ~(block - 1);
    unsigned vertical_block = info.sample_method == JPEG_DOWN_SAMPLING_YUV420 ? 16 : 8;
    unsigned padded_height = (height + vertical_block - 1) & ~(vertical_block - 1);
    if (output_size < stride * padded_height * 2) return ESP_ERR_INVALID_SIZE;
    ppa_fit_t fit;
    if (!ppa_fit(width, height, LCD_W, LCD_H, &fit)) return ESP_ERR_INVALID_SIZE;
    unsigned back = s_front ^ 1;
    ppa_srm_oper_config_t scale = {
        .in = {.buffer = s_decoded, .pic_w = stride, .pic_h = padded_height,
            .block_w = width, .block_h = height, .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .out = {.buffer = s_pixels[back], .buffer_size = LCD_W * LCD_H * 2,
            .pic_w = LCD_W, .pic_h = LCD_H, .block_offset_x = fit.x, .block_offset_y = fit.y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = fit.scale16 / 16.0f, .scale_y = fit.scale16 / 16.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    /* Borders are zeroed once at allocation. The negotiated geometry is fixed. */
    s_render_stage = "PPA DMA scale";
    start = esp_timer_get_time();
    err = ppa_do_scale_rotate_mirror(s_scaler, &scale);
    uint32_t scale_us = esp_timer_get_time() - start;
    if (err != ESP_OK) return err;
    s_render_stage = "LVGL submit";
    start = esp_timer_get_time();
    if (!bsp_display_lock(50)) return ESP_ERR_TIMEOUT;
    lv_image_set_src(s_image, &s_descriptor[back]);
    lv_obj_invalidate(s_image);
    s_front = back;
    s_display_us = frame->last_frame_us;
    bsp_display_unlock();
    video_display_result(frame->sequence, jpeg_us, scale_us, esp_timer_get_time() - start,
                         esp_timer_get_time() - frame->last_frame_us, true);
    return ESP_OK;
}

static void display_task(void *arg)
{
    uint32_t sequence = 0, errors = 0, last_capture = 0, last_display = 0;
    int64_t last_report = esp_timer_get_time();
    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        size_t size;
        video_status_t frame;
        if (!video_copy_frame(s_input, s_input_capacity, sequence, &size, &sequence, &frame)) continue;
        esp_err_t err = render_jpeg(size, &frame);
        if (err != ESP_OK) {
            video_display_result(sequence, 0, 0, 0, esp_timer_get_time() - frame.last_frame_us, false);
            if (++errors == 1 || errors % 100 == 0)
                ESP_LOGW(TAG, "LCD %s: %s (%u failures); capture continues", s_render_stage, esp_err_to_name(err), errors);
        }
        int64_t now = esp_timer_get_time();
        if (now - last_report >= 5000000) {
            video_status_t status; video_status(&status);
            float seconds = (now - last_report) / 1000000.0f;
            ESP_LOGI(TAG, "Capture %.1f fps, LCD submit %.1f fps; JPEG %"PRIu32" us, PPA %"PRIu32
                     " us, LVGL %"PRIu32" us, LCD age %"PRIu32" us, stream send %"PRIu32" us",
                     (status.sequence-last_capture)/seconds, (status.display_frames-last_display)/seconds,
                     status.jpeg_decode_us, status.scale_us, status.present_us, status.display_age_us, status.stream_send_us);
            last_report = now; last_capture = status.sequence; last_display = status.display_frames;
        }
    }
}
