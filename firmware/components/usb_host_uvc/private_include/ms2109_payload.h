/* Local MS2109 bulk quirk. See PATCHES.md. */
#pragma once
#include <stddef.h>
#include <stdint.h>
static inline size_t ms2109_continuation_header(const uint8_t *data, size_t size, uint8_t fid)
{
    /* PTS + SCR + EOH, no EOF/ERR; same frame ID. Never scan JPEG entropy data. */
    return size >= 12 && data[0] == 12 && data[1] == (uint8_t)(0x8c | (fid & 1)) ? 12 : 0;
}
