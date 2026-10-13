#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
/* Some cards pad UVC MJPEG frames after EOI. Return a bounded JPEG length. */
static inline size_t jpeg_frame_size(const uint8_t *data, size_t size)
{
    if (!data || size < 4 || data[0] != 0xff || data[1] != 0xd8) return 0;
    for (size_t end = size; end >= 4; --end)
        if (data[end - 2] == 0xff && data[end - 1] == 0xd9) return end;
    return 0;
}

/* Read bounded baseline SOF dimensions without decoding pixels. USB owns the
 * frame until publication; the LCD decoder validates the complete bitstream. */
static inline bool jpeg_frame_dimensions(const uint8_t *data, size_t size,
                                         unsigned *width, unsigned *height)
{
    if (!data || size < 4 || data[0] != 0xff || data[1] != 0xd8) return false;
    size_t pos = 2;
    while (pos < size) {
        if (data[pos++] != 0xff) return false;
        while (pos < size && data[pos] == 0xff) ++pos;
        if (pos >= size) return false;
        uint8_t marker = data[pos++];
        if (marker == 0xda || marker == 0xd9 || marker == 0 || marker == 0xd8) return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (size - pos < 2) return false;
        size_t length = ((size_t)data[pos] << 8) | data[pos + 1];
        if (length < 2 || length > size - pos) return false;
        if (marker == 0xc0) {
            if (length < 8 || data[pos + 2] != 8) return false;
            unsigned components = data[pos + 7];
            if ((components != 1 && components != 3) || length != 8 + 3 * components) return false;
            *height = ((unsigned)data[pos + 3] << 8) | data[pos + 4];
            *width = ((unsigned)data[pos + 5] << 8) | data[pos + 6];
            return *width && *height;
        }
        if ((marker >= 0xc1 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf)) return false;
        pos += length;
    }
    return false;
}
