#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { unsigned width, height, x, y, scale16; } ppa_fit_t;
/* PPA truncates scale to 1/16. Use one factor for both axes; never stretch. */
static inline bool ppa_fit(unsigned width, unsigned height, unsigned dw, unsigned dh, ppa_fit_t *fit)
{
    if (!width || !height || !dw || !dh || !fit) return false;
    uint64_t sx = (uint64_t)dw * 16 / width, sy = (uint64_t)dh * 16 / height;
    uint64_t factor64 = sx < sy ? sx : sy;
    if (!factor64 || factor64 >= 256) return false;
    unsigned factor = factor64;
    unsigned w = (uint64_t)width * factor / 16, h = (uint64_t)height * factor / 16;
    if (!w || !h || w > dw || h > dh) return false;
    *fit = (ppa_fit_t){.width=w, .height=h, .x=(dw-w)/2, .y=(dh-h)/2, .scale16=factor};
    return true;
}
