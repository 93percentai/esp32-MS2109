#include "sdkconfig.h"
#include "display.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "bsp/esp-bsp.h"
#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "video_store.h"
#include "wifi_mgr.h"
#include "provisioning.h"
#include "frame_scale.h"

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
    static uint32_t last_sequence;
    static int64_t last_us;
    int64_t now = esp_timer_get_time();
    float fps = last_us ? (video.sequence - last_sequence) * 1000000.0f / (now - last_us) : 0;
    last_us = now; last_sequence = video.sequence;
    lv_label_set_text_fmt(s_debug, "MJPEG %ux%u | %.1f fps\nJPEG %u bytes | frame %" PRIu32
        "\nDropped %" PRIu32 " | errors %" PRIu32 "\nDecode + scale %" PRIu32
        " us\nPSRAM free %u KiB\nBrowser http://%s/", video.width, video.height, fps,
        (unsigned)video.jpeg_bytes, video.sequence, video.dropped, video.errors, video.decode_us,
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
    size_t padded_w = (CONFIG_CAPTURE_WIDTH + 15) & ~15;
    size_t padded_h = (CONFIG_CAPTURE_HEIGHT + 15) & ~15;
    s_decoded = jpeg_alloc_decoder_mem(padded_w * padded_h * 2, &mem, &s_decode_capacity);
    for (int i = 0; i < 2; ++i) {
        s_pixels[i] = heap_caps_calloc(LCD_W * LCD_H, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pixels[i]) return ESP_ERR_NO_MEM;
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
    return ESP_OK;
}

esp_err_t display_jpeg(const uint8_t *data, size_t size, unsigned width, unsigned height)
{
    if (size > s_input_capacity) return ESP_ERR_INVALID_SIZE;
    memcpy(s_input, data, size);
    jpeg_decode_picture_info_t info;
    esp_err_t err = jpeg_decoder_get_info(s_input, size, &info);
    if (err != ESP_OK) return err;
    /* Refuse unexpected dimensions before hardware can DMA into our buffers. */
    if (info.width != width || info.height != height || !width || !height ||
        width > CONFIG_CAPTURE_WIDTH || height > CONFIG_CAPTURE_HEIGHT) return ESP_ERR_INVALID_SIZE;
    jpeg_decode_cfg_t config = {.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB, .conv_std = JPEG_YUV_RGB_CONV_STD_BT601};
    uint32_t output_size;
    err = jpeg_decoder_process(s_decoder, &config, s_input, size,
                               s_decoded, s_decode_capacity, &output_size);
    if (err != ESP_OK) return err;
    unsigned block = (info.sample_method == JPEG_DOWN_SAMPLING_YUV444 ||
                      info.sample_method == JPEG_DOWN_SAMPLING_GRAY) ? 8 : 16;
    unsigned stride = (width + block - 1) & ~(block - 1);
    if (output_size < stride * height * 2) return ESP_ERR_INVALID_SIZE;
    unsigned back = s_front ^ 1;
    uint16_t *dst = s_pixels[back], *src = (uint16_t *)s_decoded;
    if (!frame_scale_rgb565(src, output_size / 2, width, height, stride, dst, LCD_W, LCD_H))
        return ESP_ERR_INVALID_SIZE;
    if (!bsp_display_lock(200)) return ESP_ERR_TIMEOUT;
    lv_image_set_src(s_image, &s_descriptor[back]);
    lv_obj_invalidate(s_image);
    s_front = back;
    s_display_us = esp_timer_get_time();
    bsp_display_unlock();
    return ESP_OK;
}
