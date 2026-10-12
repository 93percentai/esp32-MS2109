#include "capture_config.h"
#include "capture.h"
#include <stdatomic.h>
#include <stdlib.h>
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/uvc_host.h"
#include "usb/usb_host.h"

#if CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE < 4096
#error "MS2109 requires CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE >= 4096; update sdkconfig in menuconfig (USB-OTG)."
#endif
#include "display.h"
#include "video_store.h"
#include "jpeg_frame.h"

static const char *TAG = "capture";
static QueueHandle_t s_frames;
static SemaphoreHandle_t s_gate;
static bool s_accept;
static atomic_bool s_disconnected;
static atomic_uint s_buffer_errors;

static bool frame_cb(const uvc_host_frame_t *frame, void *ctx)
{
    /* Callback runs in UVC task, never decode or send network data here. */
    if (xSemaphoreTake(s_gate, 0) != pdTRUE) return true;
    bool retain = s_accept && xQueueSend(s_frames, &frame, 0) == pdPASS;
    xSemaphoreGive(s_gate);
    if (!retain) video_drop(false);
    return !retain;
}

static void stream_event(const uvc_host_stream_event_data_t *event, void *ctx)
{
    switch (event->type) {
    case UVC_HOST_DEVICE_DISCONNECTED:
        ESP_LOGW(TAG, "Capture card disconnected");
        atomic_store(&s_disconnected, true);
        break;
    case UVC_HOST_TRANSFER_ERROR:
        video_drop(true);
        ESP_LOGW(TAG, "USB transfer error: %s", esp_err_to_name(event->transfer_error.error));
        break;
    case UVC_HOST_FRAME_BUFFER_OVERFLOW:
    case UVC_HOST_FRAME_BUFFER_UNDERFLOW: {
        video_drop(false);
        unsigned errors = atomic_fetch_add(&s_buffer_errors, 1) + 1;
        if (errors == 1 || errors % 100 == 0)
            ESP_LOGW(TAG, "Frame buffer %s (%u total buffer errors)",
                event->type == UVC_HOST_FRAME_BUFFER_OVERFLOW ? "overflow" : "underflow", errors);
        break;
    }
    default: break;
    }
}

static void driver_event(const uvc_host_driver_event_data_t *event, void *ctx)
{
    if (event->type != UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED) return;
    ESP_LOGI(TAG, "UVC connected: address %u, stream %u, %u modes",
        event->device_connected.dev_addr, event->device_connected.uvc_stream_index,
        (unsigned)event->device_connected.frame_info_num);
    size_t count = event->device_connected.frame_info_num;
    if (!count || count > 128) return;
    uvc_host_frame_info_t *modes = calloc(count, sizeof(*modes));
    if (!modes) return;
    if (uvc_host_get_frame_list(event->device_connected.dev_addr,
            event->device_connected.uvc_stream_index, (uvc_host_frame_info_t (*)[])modes, &count) == ESP_OK) {
        for (size_t i = 0; i < count; ++i) {
            ESP_LOGI(TAG, "Mode: format=%d %ux%u default=%.1f fps", modes[i].format,
                modes[i].h_res, modes[i].v_res,
                modes[i].default_interval ? 10000000.0 / modes[i].default_interval : 0);
        }
    }
    free(modes);
}

static void capture_task(void *arg)
{
    const uvc_host_stream_config_t config = {
        .event_cb = stream_event, .frame_cb = frame_cb,
        .usb = {.vid = CONFIG_CAPTURE_VID, .pid = CONFIG_CAPTURE_PID},
        .vs_format = {.h_res = CAPTURE_WIDTH, .v_res = CAPTURE_HEIGHT,
                      .fps = CAPTURE_FPS, .format = UVC_VS_FORMAT_MJPEG},
        .advanced = {.number_of_frame_buffers = 3, .frame_size = VIDEO_MAX_BYTES,
            .frame_heap_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            .number_of_urbs = 3, .urb_size = 16384},
    };
    while (true) {
        uvc_host_stream_hdl_t stream = NULL;
        atomic_store(&s_disconnected, false);
        esp_err_t err = uvc_host_stream_open(&config, pdMS_TO_TICKS(3000), &stream);
        if (err != ESP_OK) {
            usb_host_lib_info_t info = {0};
            usb_host_lib_info(&info);
            ESP_LOGW(TAG, "Open %ux%u MJPEG %.1f fps failed: %s; enumerated USB devices=%u",
                config.vs_format.h_res, config.vs_format.v_res, (double)config.vs_format.fps,
                esp_err_to_name(err), (unsigned)info.num_devices);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        uvc_host_desc_print(stream);
        xSemaphoreTake(s_gate, portMAX_DELAY);
        s_accept = true;
        xSemaphoreGive(s_gate);
        err = uvc_host_stream_start(stream);
        ESP_LOGI(TAG, "Stream start: %s", esp_err_to_name(err));
        video_set_connected(err == ESP_OK);
        unsigned failures = 0;
        bool first_frame = true;
        int64_t last_received = esp_timer_get_time();
        while (err == ESP_OK && !atomic_load(&s_disconnected)) {
            const uvc_host_frame_t *frame;
            if (xQueueReceive(s_frames, &frame, pdMS_TO_TICKS(500)) == pdPASS) {
                /* Decode the newest queued frame; return stale retained buffers promptly. */
                const uvc_host_frame_t *newer;
                while (xQueueReceive(s_frames, &newer, 0) == pdPASS) {
                    ESP_ERROR_CHECK(uvc_host_frame_return(stream, (uvc_host_frame_t *)frame));
                    video_drop(false);
                    frame = newer;
                }
                last_received = esp_timer_get_time();
                size_t jpeg_size = frame->data_len <= VIDEO_MAX_BYTES ?
                    jpeg_frame_size(frame->data, frame->data_len) : 0;
                if (jpeg_size) {
                    int64_t start = esp_timer_get_time();
                    esp_err_t decode = display_jpeg(frame->data, jpeg_size,
                        frame->vs_format.h_res, frame->vs_format.v_res);
                    video_set_decode_time(esp_timer_get_time() - start);
                    if (decode != ESP_OK) {
                        video_drop(true);
                        if (++failures == 1 || failures % 100 == 0)
                            ESP_LOGW(TAG, "JPEG decode failed: %s (%u failures)", esp_err_to_name(decode), failures);
                    } else {
                        if (first_frame) {
                            ESP_LOGI(TAG, "First decoded frame: %ux%u, %u JPEG bytes",
                                frame->vs_format.h_res, frame->vs_format.v_res, (unsigned)jpeg_size);
                            first_frame = false;
                        }
                        video_publish(frame->data, jpeg_size, frame->vs_format.h_res, frame->vs_format.v_res);
                    }
                } else {
                    video_drop(true);
                    if (++failures == 1 || failures % 100 == 0)
                        ESP_LOGW(TAG, "Invalid JPEG frame: %u bytes (%u failures)", (unsigned)frame->data_len, failures);
                }
                ESP_ERROR_CHECK(uvc_host_frame_return(stream, (uvc_host_frame_t *)frame));
            }
            if (esp_timer_get_time() - last_received > 10000000) {
                ESP_LOGW(TAG, "No video for 10 seconds, reopening stream");
                break;
            }
        }
        /* Only this worker owns/closes the stream. Gate late callbacks before draining. */
        xSemaphoreTake(s_gate, portMAX_DELAY);
        s_accept = false;
        xSemaphoreGive(s_gate);
        uvc_host_stream_stop(stream);
        const uvc_host_frame_t *frame;
        while (xQueueReceive(s_frames, &frame, 0) == pdPASS)
            ESP_ERROR_CHECK(uvc_host_frame_return(stream, (uvc_host_frame_t *)frame));
        video_set_connected(false);
        err = uvc_host_stream_close(stream);
        if (err != ESP_OK) ESP_LOGE(TAG, "Close failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t capture_init(void)
{
    ESP_LOGI(TAG, "USB control transfer limit=%u bytes; requested VID:PID=%04x:%04x",
        CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE, CONFIG_CAPTURE_VID, CONFIG_CAPTURE_PID);
    s_frames = xQueueCreate(2, sizeof(uvc_host_frame_t *));
    s_gate = xSemaphoreCreateMutex();
    if (!s_frames || !s_gate) return ESP_ERR_NO_MEM;
    esp_err_t err = bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, false);
    if (err != ESP_OK) return err;
    uvc_host_driver_config_t driver = {.driver_task_stack_size = 6144,
        .driver_task_priority = 11, .xCoreID = tskNO_AFFINITY,
        .create_background_task = true, .event_cb = driver_event};
    err = uvc_host_install(&driver);
    if (err != ESP_OK) return err;
    return xTaskCreate(capture_task, "capture", 6144, NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
