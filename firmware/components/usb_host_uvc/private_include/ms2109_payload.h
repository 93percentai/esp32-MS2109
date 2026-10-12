/* Local MS2109 bulk quirk. See PATCHES.md. */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* At known-card 16KiB boundaries, recognize FID/EOF/ERR with PTS+SCR+EOH. */
static inline size_t ms2109_payload_header(const uint8_t *data, size_t size)
{
    return size >= 12 && data[0] == 12 && (data[1] & 0xbc) == 0x8c ? 12 : 0;
}
