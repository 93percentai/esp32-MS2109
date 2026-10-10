#pragma once
#include <stddef.h>
#include <stdint.h>
/* Some cards pad UVC MJPEG frames after EOI. Return a bounded JPEG length. */
static inline size_t jpeg_frame_size(const uint8_t *data, size_t size)
{
    if (!data || size < 4 || data[0] != 0xff || data[1] != 0xd8) return 0;
    for (size_t end = size; end >= 4; --end)
        if (data[end - 2] == 0xff && data[end - 1] == 0xd9) return end;
    return 0;
}
