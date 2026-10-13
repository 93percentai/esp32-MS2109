#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#define VIDEO_MAX_BYTES (CONFIG_CAPTURE_FRAME_KB * 1024U)
typedef struct {
    bool connected, stream_active;
    unsigned width, height;
    uint32_t sequence, dropped, errors;
    size_t jpeg_bytes;
    int64_t last_frame_us;
    uint32_t decode_us;
    uint32_t display_frames, display_errors, display_sequence;
    uint32_t jpeg_decode_us, scale_us, present_us, display_age_us;
    uint32_t stream_frames, stream_skipped, stream_aborts, stream_send_us;
} video_status_t;
esp_err_t video_store_init(void);
void video_publish(const uint8_t *data, size_t size, unsigned width, unsigned height);
/* Copy while locked; never retain a pointer into a producer buffer. */
bool video_copy(uint8_t *out, size_t capacity, uint32_t after_sequence,
                size_t *size, uint32_t *sequence);
/* Metadata and bytes are taken from the same publication under one lock. */
bool video_copy_frame(uint8_t *out, size_t capacity, uint32_t after_sequence,
                      size_t *size, uint32_t *sequence, video_status_t *metadata);
void video_display_result(uint32_t sequence, uint32_t jpeg_us, uint32_t scale_us,
                          uint32_t present_us, uint32_t age_us, bool success);
void video_stream_result(uint32_t send_us, uint32_t skipped, bool success);
void video_set_stream_active(bool active);
void video_status(video_status_t *out);
void video_set_connected(bool connected);
void video_drop(bool error);
