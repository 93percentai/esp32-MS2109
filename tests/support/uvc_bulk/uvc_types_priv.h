#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "esp_err.h"
typedef struct {
    uint8_t bHeaderLength;
    struct { uint8_t frame_id:1, end_of_frame:1, pts:1, scr:1, reserved:1, still:1, error:1, eoh:1; } bmHeaderInfo;
} uvc_payload_header_t;
typedef struct { uint8_t data[65536]; size_t data_len; } uvc_host_frame_t;
typedef struct { int type; } uvc_host_stream_event_data_t;
typedef void (*uvc_host_stream_callback_t)(const uvc_host_stream_event_data_t *, void *);
#define UVC_HOST_FRAME_BUFFER_OVERFLOW 1
#define UVC_HOST_FRAME_BUFFER_UNDERFLOW 2
#define UVC_VS_FORMAT_MJPEG 1
#define JPEG_MARKER 0xff
#define JPEG_SOI 0xd8
#define UVC_STREAM_BULK_PACKET_SOF 0
#define UVC_STREAM_BULK_PACKET_DATA 1
#define UVC_STREAM_BULK_PACKET_EOF 2
typedef struct {
 struct { bool ms2109_headers; bool (*frame_cb)(const uvc_host_frame_t *,void *); void *cb_arg; uvc_host_stream_callback_t stream_cb; } constant;
 struct { bool streaming; uvc_host_frame_t *current_frame; struct {int format;} vs_format; } dynamic;
 struct {int next_bulk_packet; bool skip_current_frame; uint8_t current_frame_id;} single_thread;
} uvc_stream_t;
typedef struct { void *context; uint8_t *data_buffer; int actual_num_bytes, data_buffer_size, status; } usb_transfer_t;
enum { USB_TRANSFER_STATUS_COMPLETED, USB_TRANSFER_STATUS_NO_DEVICE, USB_TRANSFER_STATUS_CANCELED,
 USB_TRANSFER_STATUS_ERROR, USB_TRANSFER_STATUS_OVERFLOW, USB_TRANSFER_STATUS_STALL,
 USB_TRANSFER_STATUS_TIMED_OUT, USB_TRANSFER_STATUS_SKIPPED };
#define UVC_ENTER_CRITICAL() ((void)0)
#define UVC_EXIT_CRITICAL() ((void)0)
#define UVC_ATOMIC_LOAD(x) (x)
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
#define ESP_LOGE(...) ((void)TAG)
#define ESP_LOGW(...) ((void)TAG)
#define ESP_LOGD(...) ((void)TAG)
esp_err_t uvc_host_frame_return(uvc_stream_t *, uvc_host_frame_t *);
esp_err_t uvc_host_stream_pause(uvc_stream_t *);
esp_err_t uvc_frame_add_data(uvc_host_frame_t *, const uint8_t *, size_t);
uvc_host_frame_t *uvc_frame_get_empty(uvc_stream_t *);
void uvc_frame_reset(uvc_host_frame_t *);
bool uvc_frame_payload_header_validate(const uvc_payload_header_t *, size_t);
int usb_host_transfer_submit(usb_transfer_t *);
