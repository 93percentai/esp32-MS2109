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
    for (unsigned y = 0; y < fh; ++y)
        for (unsigned x = 0; x < fw; ++x)
            dst[(size_t)(y + y0) * dw + x + x0] =
                src[(size_t)(y * (uint64_t)height / fh) * stride + x * (uint64_t)width / fw];
    return true;
}
