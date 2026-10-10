#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "form_codec.h"
#include "frame_scale.h"
#include "ms2109_payload.h"
#include "dns_packet.h"
#include "jpeg_frame.h"

static void test_forms(void)
{
    char value[64];
    assert(form_get("ssid=a%26b+%2B%25&pass=12345678", "ssid", value, sizeof(value)));
    assert(!strcmp(value, "a&b +%"));
    assert(form_get("pass=&custom=", "pass", value, sizeof(value)) && !value[0]);
    assert(!form_get("pass=bad%", "pass", value, sizeof(value)));
    assert(!form_get("pass=bad%2", "pass", value, sizeof(value)));
    assert(!form_get("pass=bad%XY", "pass", value, sizeof(value)));
    assert(!form_get("pass=abc%00def", "pass", value, sizeof(value)));
    assert(!form_get("pass=a&pass=b", "pass", value, sizeof(value)));
    assert(!form_get("password=a", "pass", value, sizeof(value)));
    char ssid[33];
    assert(form_get("ssid=12345678901234567890123456789012", "ssid", ssid, sizeof(ssid)));
    assert(strlen(ssid) == 32);
    assert(!form_get("ssid=123456789012345678901234567890123", "ssid", ssid, sizeof(ssid)));
    char escaped[200];
    assert(html_escape("<script>&\"'", escaped, sizeof(escaped)) == strlen(escaped));
    assert(!strcmp(escaped, "&lt;script&gt;&amp;&quot;&#39;"));
    char tiny[5] = {0};
    html_escape("&&", tiny, sizeof(tiny));
    assert(!tiny[0]); /* Never output half an entity or run beyond the buffer. */
    memset(ssid, '"', 32); ssid[32] = 0;
    assert(html_escape(ssid, escaped, sizeof(escaped)) == 192);
}

static void test_bulk_headers(void)
{
    uint8_t payload[20] = {12, 0x8c};
    for (size_t n = 0; n < 12; ++n) assert(ms2109_continuation_header(payload, n, 0) == 0);
    assert(ms2109_continuation_header(payload, sizeof(payload), 0) == 12);
    assert(ms2109_continuation_header(payload, sizeof(payload), 1) == 0);
    payload[1] = 0x8d;
    assert(ms2109_continuation_header(payload, sizeof(payload), 1) == 12);
    payload[1] = 0x8e; /* EOF: let the regular state machine handle it. */
    assert(ms2109_continuation_header(payload, sizeof(payload), 0) == 0);
    payload[1] = 0xcc; /* ERR: never disguise a bad packet. */
    assert(ms2109_continuation_header(payload, sizeof(payload), 0) == 0);
    payload[0] = 0xff; payload[1] = 0xd8;
    assert(ms2109_continuation_header(payload, sizeof(payload), 0) == 0);
    assert(ms2109_continuation_header(NULL, 0, 0) == 0);
}

static void test_scale(void)
{
    uint16_t src[] = {1,2,3,4,99,99,5,6,7,8,99,99}; /* padded source stride */
    uint16_t dst[18];
    dst[0] = 1234; dst[17] = 5678;
    assert(frame_scale_rgb565(src, 12, 4, 2, 6, dst + 1, 4, 4));
    assert(dst[0] == 1234 && dst[17] == 5678);
    uint16_t expected[] = {0,0,0,0,1,2,3,4,5,6,7,8,0,0,0,0};
    assert(!memcmp(dst + 1, expected, sizeof(expected)));
    assert(!frame_scale_rgb565(src, 11, 4, 2, 6, dst + 1, 4, 4));
    assert(!frame_scale_rgb565(src, 12, 4, 2, 3, dst + 1, 4, 4));
    assert(!frame_scale_rgb565(src, 12, 0, 2, 6, dst + 1, 4, 4));
    uint16_t portrait[] = {1,2,3,4,5,6,7,8};
    assert(frame_scale_rgb565(portrait, 8, 2, 4, 2, dst + 1, 4, 4));
    uint16_t vertical[] = {0,1,2,0,0,3,4,0,0,5,6,0,0,7,8,0};
    assert(!memcmp(dst + 1, vertical, sizeof(vertical)));
}

static void test_dns(void)
{
    uint8_t query[32] = {0x12,0x34,1,0,0,1,0,0,0,0,0,0,1,'a',0,0,1,0,1};
    uint8_t ip[] = {192,168,4,1}, reply[64];
    assert(captive_dns_reply(query, 19, ip, reply, sizeof(reply)) == 35);
    assert(reply[0] == 0x12 && reply[1] == 0x34 && reply[2] == 0x85 && reply[7] == 1);
    assert(!memcmp(reply + 31, ip, 4));
    assert(!captive_dns_reply(query, 19, ip, reply, 34));
    for (size_t n = 0; n < 19; ++n) assert(!captive_dns_reply(query, n, ip, reply, sizeof(reply)));
    query[16] = 28; /* AAAA must get a valid response with no IPv4 answer. */
    assert(captive_dns_reply(query, 19, ip, reply, sizeof(reply)) == 19 && reply[7] == 0);
    query[12] = 0xc0; /* compressed/malformed question rejected, no loops */
    assert(!captive_dns_reply(query, 19, ip, reply, sizeof(reply)));
    query[12] = 63; /* label runs past packet */
    assert(!captive_dns_reply(query, 19, ip, reply, sizeof(reply)));
    query[12] = 1; query[5] = 2;
    assert(!captive_dns_reply(query, 19, ip, reply, sizeof(reply)));
}

int main(void)
{
    uint8_t jpeg[] = {0xff,0xd8,1,2,0xff,0xd9,0,0,0};
    assert(jpeg_frame_size(jpeg, sizeof(jpeg)) == 6);
    assert(jpeg_frame_size(jpeg, 6) == 6);
    for (size_t size = 0; size < 6; ++size) assert(!jpeg_frame_size(jpeg, size));
    assert(!jpeg_frame_size(NULL, 0));
    jpeg[0] = 0;
    assert(!jpeg_frame_size(jpeg, sizeof(jpeg)));
    test_forms(); test_bulk_headers(); test_scale(); test_dns();
    puts("PASS: portal decoding/escaping, MS2109 boundary headers, RGB565 scaling, captive DNS bounds");
}
