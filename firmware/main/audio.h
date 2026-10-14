#pragma once
#include "esp_err.h"
#include "pcm_ring.h"
typedef struct {
    bool enabled, connected, speaker_ready, muted;
    unsigned volume;
    uint32_t packets, errors, speaker_skipped, network_skipped;
    int64_t last_audio_us;
} audio_status_t;
/* Call after capture_init has installed the shared BSP USB host. */
esp_err_t audio_init(void);
void audio_status(audio_status_t *out);
bool audio_copy(pcm_cursor_t *cursor,uint8_t *out,size_t capacity,pcm_info_t *info,bool network);
bool audio_control(unsigned volume,bool muted);
