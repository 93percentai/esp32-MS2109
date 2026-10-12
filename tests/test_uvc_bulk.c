#include <stdio.h>
#include <string.h>
#include "uvc_types_priv.h"

static uvc_host_frame_t buffer;
static bool occupied;
static unsigned completed, submitted;
static uint8_t latest[65536];
static size_t latest_size;
void bulk_transfer_callback(usb_transfer_t *transfer);

esp_err_t uvc_host_frame_return(uvc_stream_t *s, uvc_host_frame_t *f)
{ (void)s; assert(f == &buffer && occupied); occupied = false; return ESP_OK; }
esp_err_t uvc_host_stream_pause(uvc_stream_t *s)
{ s->dynamic.streaming = false; return ESP_OK; }
esp_err_t uvc_frame_add_data(uvc_host_frame_t *f, const uint8_t *data, size_t size)
{ assert(f->data_len + size <= sizeof(f->data)); memcpy(f->data + f->data_len, data, size); f->data_len += size; return ESP_OK; }
uvc_host_frame_t *uvc_frame_get_empty(uvc_stream_t *s)
{ (void)s; if (occupied) return NULL; occupied = true; buffer.data_len = 0; return &buffer; }
void uvc_frame_reset(uvc_host_frame_t *f) { f->data_len = 0; }
bool uvc_frame_payload_header_validate(const uvc_payload_header_t *h, size_t n)
{ return n >= 2 && h->bHeaderLength >= 2 && h->bHeaderLength <= n && h->bmHeaderInfo.eoh; }
int usb_host_transfer_submit(usb_transfer_t *t) { (void)t; ++submitted; return 0; }
static bool frame(const uvc_host_frame_t *f, void *ctx)
{ (void)ctx; ++completed; latest_size = f->data_len; memcpy(latest, f->data, latest_size); return true; }
static uvc_stream_t stream(bool quirk)
{ return (uvc_stream_t){.constant={.ms2109_headers=quirk,.frame_cb=frame},
    .dynamic={.streaming=true,.vs_format={.format=UVC_VS_FORMAT_MJPEG}},
    .single_thread={.current_frame_id=2}}; }
static void transfer(uvc_stream_t *s, uint8_t *data, size_t size)
{ usb_transfer_t t={.context=s,.data_buffer=data,.actual_num_bytes=(int)size,.data_buffer_size=16384};
    unsigned before=submitted; bulk_transfer_callback(&t); assert(submitted == before + 1); }
static void packet(uint8_t *out, size_t size, uint8_t flags, uint8_t fill)
{ memset(out, fill, size); out[0]=12; out[1]=flags; memset(out+2,0,10); }

int main(void)
{
    uint8_t payload[16384];
    uvc_stream_t s=stream(true);
    transfer(&s,payload,0); /* SOF ZLP must not poison the next frame. */
    packet(payload,16384,0x8c,0x11);payload[12]=0xff;payload[13]=0xd8;
    transfer(&s,payload,16384);
    packet(payload,16384,0x8c,0x22);transfer(&s,payload,16384);
    transfer(&s,payload,0);transfer(&s,payload,0); /* DATA and EOF ZLPs */
    packet(payload,14,0x8e,0);payload[12]=0xff;payload[13]=0xd9;
    transfer(&s,payload,14);
    assert(completed == 1 && latest_size == 2*16372+2 && !occupied);
    assert(latest[0]==0xff && latest[1]==0xd8);
    assert(latest[16371]==0x11 && latest[16372]==0x22);
    assert(latest[latest_size-2]==0xff && latest[latest_size-1]==0xd9);
    /* Complete single-transfer frame with EOF: no next frame is needed. */
    transfer(&s,payload,0);
    packet(payload,16,0x8f,0);payload[12]=0xff;payload[13]=0xd8;payload[14]=0xff;payload[15]=0xd9;
    transfer(&s,payload,16);assert(completed==2 && latest_size==4);
    /* A header-only SOF does not consume a buffer or lose the following SOI. */
    packet(payload,12,0x8c,0);transfer(&s,payload,12);assert(!occupied);
    packet(payload,16,0x8e,0);payload[12]=0xff;payload[13]=0xd8;payload[14]=0xff;payload[15]=0xd9;
    transfer(&s,payload,16);assert(completed==3 && latest_size==4);
    /* ERR on final payload suppresses the frame, and the next FID recovers. */
    packet(payload,16384,0x8d,0x33);payload[12]=0xff;payload[13]=0xd8;transfer(&s,payload,16384);
    packet(payload,14,0xcf,0);payload[12]=0xff;payload[13]=0xd9;transfer(&s,payload,14);
    assert(completed==3 && !occupied);
    /* FID switch after a missing EOF completes old data without mixing frames. */
    packet(payload,16,0x8c,0);payload[12]=0xff;payload[13]=0xd8;payload[14]=0xff;payload[15]=0xd9;
    transfer(&s,payload,16);transfer(&s,payload,0);
    packet(payload,16,0x8f,0);payload[12]=0xff;payload[13]=0xd8;payload[14]=0xff;payload[15]=0xd9;
    transfer(&s,payload,16);assert(completed==5 && latest_size==4 && !occupied);
    /* No free buffer: discard the whole frame, then recover next SOF. */
    occupied=true;
    packet(payload,16384,0x8c,0x44);payload[12]=0xff;payload[13]=0xd8;
    transfer(&s,payload,16384);assert(!s.dynamic.current_frame);
    occupied=false;
    packet(payload,14,0x8e,0);payload[12]=0xff;payload[13]=0xd9;
    transfer(&s,payload,14);assert(completed==5 && !occupied);
    /* Transfers without a continuation header are still raw JPEG data. */
    packet(payload,16384,0x8d,0x55);payload[12]=0xff;payload[13]=0xd8;transfer(&s,payload,16384);
    memset(payload,0x66,16384);transfer(&s,payload,16384);
    packet(payload,14,0x8f,0);payload[12]=0xff;payload[13]=0xd9;transfer(&s,payload,14);
    assert(completed==6 && latest_size==16372+16384+2 && latest[16372]==0x66 && !occupied);
    /* Standard (unpatched) devices also tolerate delimiter ZLPs. */
    s=stream(false);transfer(&s,payload,0);
    packet(payload,16,0x8c,0);payload[12]=0xff;payload[13]=0xd8;payload[14]=0xff;payload[15]=0xd9;
    transfer(&s,payload,16);transfer(&s,payload,0);
    packet(payload,12,0x8e,0);transfer(&s,payload,12);
    assert(completed==7 && latest_size==4 && !occupied);
    puts("PASS: actual UVC bulk callback: ZLP, final headers, FID recovery, ERR, single-transfer and generic frames");
}
