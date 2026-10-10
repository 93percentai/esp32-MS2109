#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "video_store.h"

int64_t test_clock_us = 1000000;
static void make_jpeg(uint8_t *data, uint8_t value)
{
    memset(data, value, 64);
    data[0] = 0xff; data[1] = 0xd8; data[62] = 0xff; data[63] = 0xd9;
}
static void *producer(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 10000; ++i) {
        uint8_t frame[64]; make_jpeg(frame, i & 0xff);
        video_publish(frame, sizeof(frame), 640, 480);
    }
    return NULL;
}
int main(void)
{
    assert(video_store_init() == ESP_OK);
    uint8_t frame[64], copy[64]; size_t size; uint32_t sequence;
    make_jpeg(frame, 42);
    video_set_connected(true);
    video_publish(frame, sizeof(frame), 640, 480);
    assert(video_copy(copy, sizeof(copy), 0, &size, &sequence));
    assert(size == 64 && sequence == 1 && copy[10] == 42);
    assert(!video_copy(copy, 63, 0, &size, &sequence));
    assert(!video_copy(copy, sizeof(copy), sequence, &size, &sequence));
    /* A slow consumer's own copy must survive subsequent producer writes. */
    make_jpeg(frame, 99); video_publish(frame, sizeof(frame), 640, 480);
    assert(copy[10] == 42);
    assert(video_copy(copy, sizeof(copy), sequence, &size, &sequence) && copy[10] == 99);
    test_clock_us += 3000000;
    assert(!video_copy(copy, sizeof(copy), 0, &size, &sequence));
    video_set_connected(false);
    video_status_t status; video_status(&status);
    assert(!status.jpeg_bytes && !status.last_frame_us);
    assert(!video_copy(copy, sizeof(copy), 0, &size, &sequence));
    video_set_connected(true);
    frame[0] = 0; video_publish(frame, sizeof(frame), 640, 480);
    video_status(&status); assert(status.errors == 1 && status.dropped == 1);
    /* Exercise real concurrent publication/copy, checking for torn frames. */
    pthread_t thread; assert(!pthread_create(&thread, NULL, producer, NULL));
    for (unsigned i = 0; i < 10000; ++i) {
        if (video_copy(copy, sizeof(copy), 0, &size, &sequence)) {
            assert(size == 64 && copy[0] == 0xff && copy[63] == 0xd9);
            for (unsigned j = 3; j < 62; ++j) assert(copy[j] == copy[2]);
        }
    }
    assert(!pthread_join(thread, NULL));
    puts("PASS: fresh snapshots, stale/disconnected rejection, consumer ownership, concurrent frame copies");
}
