#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#define VIDEO_MAX_BYTES (CONFIG_CAPTURE_FRAME_KB * 1024U)
typedef struct {
    bool connected;
    unsigned width, height;
    uint32_t sequence, dropped, errors;
    size_t jpeg_bytes;
    int64_t last_frame_us;
    uint32_t decode_us;
} video_status_t;
esp_err_t video_store_init(void);
void video_publish(const uint8_t *data, size_t size, unsigned width, unsigned height);
/* Copy while locked; never retain a pointer into a producer buffer. */
bool video_copy(uint8_t *out, size_t capacity, uint32_t after_sequence,
                size_t *size, uint32_t *sequence);
void video_status(video_status_t *out);
void video_set_connected(bool connected);
void video_drop(bool error);
void video_set_decode_time(uint32_t us);
