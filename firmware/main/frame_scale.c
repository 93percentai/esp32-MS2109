#include "frame_scale.h"
#include <string.h>
bool frame_scale_rgb565(const uint16_t *src, size_t src_pixels, unsigned width,
    unsigned height, unsigned stride, uint16_t *dst, unsigned dw, unsigned dh)
{
    if (!src || !dst || !width || !height || !dw || !dh || stride < width ||
        (size_t)stride * height > src_pixels) return false;
    memset(dst, 0, (size_t)dw * dh * sizeof(*dst));
    unsigned fw = dw, fh = (uint64_t)height * dw / width;
    if (fh > dh) { fh = dh; fw = (uint64_t)width * dh / height; }
    if (!fw || !fh) return false;
    unsigned x0 = (dw - fw) / 2, y0 = (dh - fh) / 2;
    /* Bresenham-style coordinates give floor(x * width / fw) without a
     * 64-bit divide for every LCD pixel, keeping capture buffers available. */
    unsigned x_step = width / fw, x_rem = width % fw;
    unsigned y_step = height / fh, y_rem = height % fh, sy = 0;
    uint64_t y_error = 0;
    for (unsigned y = 0; y < fh; ++y) {
        const uint16_t *row = src + (size_t)sy * stride;
        uint16_t *out = dst + (size_t)(y + y0) * dw + x0;
        unsigned sx = 0;
        uint64_t x_error = 0;
        for (unsigned x = 0; x < fw; ++x) {
            out[x] = row[sx];
            sx += x_step;
            x_error += x_rem;
            if (x_error >= fw) { x_error -= fw; ++sx; }
        }
        sy += y_step;
        y_error += y_rem;
        if (y_error >= fh) { y_error -= fh; ++sy; }
    }
    return true;
}
