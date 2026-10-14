#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PCM_RATE 48000U
#define PCM_CHANNELS 2U
#define PCM_BITS 16U
#define PCM_BLOCK_BYTES 3840U /* 20 ms, signed stereo PCM16 little-endian */
#define PCM_RING_SLOTS 6U
#define PCM_HEADER_BYTES 24U

typedef struct { uint32_t generation; uint64_t sequence; } pcm_cursor_t;
typedef struct { uint32_t generation, sequence, skipped; int64_t timestamp_us; } pcm_info_t;
typedef struct {
    uint8_t *data;
    uint32_t generation;
    uint64_t sequence;
    int64_t timestamps[PCM_RING_SLOTS];
} pcm_ring_t;
/* Caller serializes all access; consumers receive independent copies. */
void pcm_ring_init(pcm_ring_t *ring, uint8_t *storage);
void pcm_ring_reset(pcm_ring_t *ring);
bool pcm_ring_push(pcm_ring_t *ring, const uint8_t *data, size_t size, int64_t timestamp_us);
bool pcm_ring_read(const pcm_ring_t *ring, pcm_cursor_t *cursor, uint8_t *out,
                   size_t capacity, unsigned max_backlog, pcm_info_t *info);
void pcm_wire_header(uint8_t out[PCM_HEADER_BYTES], const pcm_info_t *info);
bool pcm_stereo_to_dual_mono(uint8_t *data,size_t size);
