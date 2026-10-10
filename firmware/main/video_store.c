#include "sdkconfig.h"
#include "video_store.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static uint8_t *s_jpeg;
static video_status_t s_status;

esp_err_t video_store_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_jpeg = heap_caps_malloc(VIDEO_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_lock && s_jpeg ? ESP_OK : ESP_ERR_NO_MEM;
}

void video_publish(const uint8_t *data, size_t size, unsigned width, unsigned height)
{
    if (size > VIDEO_MAX_BYTES || size < 4 || data[0] != 0xff || data[1] != 0xd8 ||
        data[size - 2] != 0xff || data[size - 1] != 0xd9) {
        video_drop(true);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_jpeg, data, size);
    s_status.jpeg_bytes = size;
    s_status.width = width;
    s_status.height = height;
    ++s_status.sequence;
    if (!s_status.sequence) ++s_status.sequence;
    s_status.last_frame_us = esp_timer_get_time();
    xSemaphoreGive(s_lock);
}

bool video_copy(uint8_t *out, size_t capacity, uint32_t after_sequence,
                size_t *size, uint32_t *sequence)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ready = s_status.connected && s_status.jpeg_bytes &&
                 s_status.sequence != after_sequence && s_status.jpeg_bytes <= capacity &&
                 esp_timer_get_time() - s_status.last_frame_us < 3000000;
    if (ready) {
        memcpy(out, s_jpeg, s_status.jpeg_bytes);
        *size = s_status.jpeg_bytes;
        *sequence = s_status.sequence;
    }
    xSemaphoreGive(s_lock);
    return ready;
}

void video_status(video_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

void video_set_connected(bool connected)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.connected = connected;
    if (!connected) { s_status.jpeg_bytes = 0; s_status.last_frame_us = 0; }
    xSemaphoreGive(s_lock);
}

void video_drop(bool error)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ++s_status.dropped;
    if (error) ++s_status.errors;
    xSemaphoreGive(s_lock);
}

void video_set_decode_time(uint32_t us)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.decode_us = us;
    xSemaphoreGive(s_lock);
}
