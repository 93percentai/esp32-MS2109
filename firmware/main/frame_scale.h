#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool frame_scale_rgb565(const uint16_t *src, size_t src_pixels, unsigned width,
    unsigned height, unsigned stride, uint16_t *dst, unsigned dw, unsigned dh);
