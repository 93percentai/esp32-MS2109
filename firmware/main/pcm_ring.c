#include "pcm_ring.h"
#include <string.h>
#include <limits.h>
void pcm_ring_init(pcm_ring_t *ring, uint8_t *storage)
{ *ring = (pcm_ring_t){.data=storage,.generation=1}; }
void pcm_ring_reset(pcm_ring_t *ring)
{ if (!++ring->generation) ++ring->generation; ring->sequence=0; }
bool pcm_ring_push(pcm_ring_t *ring, const uint8_t *data, size_t size, int64_t timestamp_us)
{
    if (!ring->data || !data || size!=PCM_BLOCK_BYTES) return false;
    ++ring->sequence;
    unsigned slot=(ring->sequence-1)%PCM_RING_SLOTS;
    memcpy(ring->data+slot*PCM_BLOCK_BYTES,data,size);
    ring->timestamps[slot]=timestamp_us;
    return true;
}
bool pcm_ring_read(const pcm_ring_t *ring, pcm_cursor_t *cursor, uint8_t *out,
                   size_t capacity, unsigned max_backlog, pcm_info_t *info)
{
    if (!ring->data || !ring->sequence || !out || !cursor || !info || capacity<PCM_BLOCK_BYTES || !max_backlog) return false;
    uint64_t next=ring->sequence, skipped=0;
    if (cursor->generation==ring->generation && cursor->sequence<=ring->sequence) {
        if (cursor->sequence==ring->sequence) return false;
        uint64_t backlog=ring->sequence-cursor->sequence;
        if (backlog<=max_backlog && backlog<=PCM_RING_SLOTS) next=cursor->sequence+1;
        else skipped=backlog-1;
    }
    unsigned slot=(next-1)%PCM_RING_SLOTS;
    memcpy(out,ring->data+slot*PCM_BLOCK_BYTES,PCM_BLOCK_BYTES);
    *info=(pcm_info_t){.generation=ring->generation,.sequence=(uint32_t)next,
        .skipped=skipped>UINT32_MAX ? UINT32_MAX : skipped,.timestamp_us=ring->timestamps[slot]};
    *cursor=(pcm_cursor_t){.generation=ring->generation,.sequence=next};
    return true;
}
static void little32(uint8_t *out,uint32_t value)
{ for(unsigned i=0;i<4;++i) out[i]=(uint8_t)(value>>(8*i)); }
void pcm_wire_header(uint8_t out[PCM_HEADER_BYTES],const pcm_info_t *info)
{
    memcpy(out,"PCM1",4);little32(out+4,info->sequence);little32(out+8,PCM_RATE);
    out[12]=PCM_CHANNELS;out[13]=0;out[14]=PCM_BITS;out[15]=0;
    little32(out+16,PCM_BLOCK_BYTES);little32(out+20,info->generation);
}

bool pcm_stereo_to_dual_mono(uint8_t *data,size_t size)
{
    if(!data || size%4) return false;
    for(size_t i=0;i<size;i+=4) {
        int16_t left=(int16_t)((unsigned)data[i]|((unsigned)data[i+1]<<8));
        int16_t right=(int16_t)((unsigned)data[i+2]|((unsigned)data[i+3]<<8));
        uint16_t mono=(uint16_t)(int16_t)(((int32_t)left+right)/2);
        data[i]=data[i+2]=(uint8_t)mono;data[i+1]=data[i+3]=(uint8_t)(mono>>8);
    }
    return true;
}
