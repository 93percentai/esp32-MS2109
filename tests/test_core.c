#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
    for (size_t n = 0; n < 12; ++n) assert(ms2109_payload_header(payload, n) == 0);
    assert(ms2109_payload_header(payload, sizeof(payload)) == 12);
    payload[0] = 0xff; payload[1] = 0xd8;
    assert(ms2109_payload_header(payload, sizeof(payload)) == 0);
    payload[0] = 12;
    for (unsigned flags = 0; flags < 256; ++flags) {
        payload[1] = flags;
        assert(ms2109_payload_header(payload, 11) == 0);
        bool recognized = flags == 0x8c || flags == 0x8d || flags == 0x8e || flags == 0x8f ||
                          flags == 0xcc || flags == 0xcd || flags == 0xce || flags == 0xcf;
        assert(ms2109_payload_header(payload, sizeof(payload)) == (recognized ? 12 : 0));
    }
    assert(ms2109_payload_header(NULL, 0) == 0);
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

static void test_scale_reference(void)
{
    uint16_t src[25*23], actual[17*17], reference[17*17];
    unsigned seed = 7;
    for (unsigned test = 0; test < 1000; ++test) {
        seed = seed * 1664525u + 1013904223u;
        unsigned width = 1 + seed % 23, height = 1 + (seed >> 8) % 23;
        unsigned dw = 1 + (seed >> 16) % 17, dh = 1 + (seed >> 24) % 17;
        unsigned stride = width + 2;
        for (unsigned i = 0; i < stride*height; ++i) src[i] = (uint16_t)(i+1);
        unsigned fw = dw, fh = (uint64_t)height * dw / width;
        if (fh > dh) { fh = dh; fw = (uint64_t)width * dh / height; }
        bool valid = fw && fh;
        assert(frame_scale_rgb565(src, stride*height, width, height, stride, actual, dw, dh) == valid);
        memset(reference, 0, dw*dh*2);
        if (valid) {
            for (unsigned y = 0; y < fh; ++y)
                for (unsigned x = 0; x < fw; ++x)
                    reference[(y+(dh-fh)/2)*dw+x+(dw-fw)/2] =
                        src[(y*(uint64_t)height/fh)*stride+x*(uint64_t)width/fw];
        }
        assert(!memcmp(actual, reference, dw*dh*2));
    }
    /* 720p colour bars become 800x450 with 15 black rows above/below. */
    uint16_t *hd = calloc(1280*720, 2), *lcd = calloc(800*480, 2);
    assert(hd && lcd);
    const uint16_t bars[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    for (unsigned y = 0; y < 720; ++y)
        for (unsigned x = 0; x < 1280; ++x) hd[y*1280+x] = bars[x/320];
    assert(frame_scale_rgb565(hd, 1280*720, 1280, 720, 1280, lcd, 800, 480));
    for (unsigned y = 0; y < 480; ++y)
        for (unsigned x = 0; x < 800; ++x)
            assert(lcd[y*800+x] == (y < 15 || y >= 465 ? 0 : bars[x/200]));
    free(hd); free(lcd);
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
    test_forms(); test_bulk_headers(); test_scale(); test_scale_reference(); test_dns();
    puts("PASS: portal decoding/escaping, MS2109 boundary headers, RGB565 scaling, captive DNS bounds");
}
