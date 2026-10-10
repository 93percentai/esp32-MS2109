#include "dns_packet.h"
#include <string.h>
size_t captive_dns_reply(const uint8_t *query, size_t size, const uint8_t ip[4],
                         uint8_t *reply, size_t capacity)
{
    if (!query || !ip || !reply || size < 12 || query[4] || query[5] != 1 ||
        (query[2] & 0xf8)) return 0;
    size_t cursor = 12;
    while (cursor < size && query[cursor]) {
        size_t label = query[cursor++];
        if (label > 63 || label > size - cursor) return 0;
        cursor += label;
    }
    if (cursor >= size || size - ++cursor < 4) return 0;
    int answer = query[cursor] == 0 && query[cursor + 1] == 1 &&
                 query[cursor + 2] == 0 && query[cursor + 3] == 1;
    cursor += 4;
    size_t result = cursor + (answer ? 16 : 0);
    if (result > capacity) return 0;
    memcpy(reply, query, cursor);
    reply[2] = 0x84 | (query[2] & 1);
    reply[3] = 0;
    reply[6] = 0; reply[7] = answer;
    reply[8] = reply[9] = reply[10] = reply[11] = 0;
    if (answer) {
        const uint8_t fields[] = {0xc0,12,0,1,0,1,0,0,0,30,0,4};
        memcpy(reply + cursor, fields, sizeof(fields));
        memcpy(reply + cursor + sizeof(fields), ip, 4);
    }
    return result;
}
