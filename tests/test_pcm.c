#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pcm_ring.h"
#include "audio_control_form.h"
int main(int argc,char **argv)
{
    uint8_t *storage=malloc(PCM_BLOCK_BYTES*PCM_RING_SLOTS);assert(storage);
    pcm_ring_t ring;pcm_ring_init(&ring,storage);
    pcm_cursor_t speaker={0},browser={0};pcm_info_t info;
    uint8_t block[PCM_BLOCK_BYTES],copy[PCM_BLOCK_BYTES],snapshot[PCM_BLOCK_BYTES];
    assert(!pcm_ring_read(&ring,&speaker,copy,sizeof(copy),3,&info));
    memset(block,1,sizeof(block));assert(!pcm_ring_push(&ring,block,sizeof(block)-1,1));
    assert(pcm_ring_push(&ring,block,sizeof(block),20000));
    assert(pcm_ring_read(&ring,&speaker,copy,sizeof(copy),3,&info));
    assert(info.sequence==1&&info.timestamp_us==20000&&copy[0]==1);
    assert(pcm_ring_read(&ring,&browser,snapshot,sizeof(snapshot),3,&info));
    assert(!pcm_ring_read(&ring,&speaker,copy,sizeof(copy),3,&info));
    memset(block,2,sizeof(block));assert(pcm_ring_push(&ring,block,sizeof(block),40000));
    assert(snapshot[0]==1); /* Playback and send copies survive producer overwrite. */
    assert(!pcm_ring_read(&ring,&speaker,copy,sizeof(copy)-1,3,&info));
    assert(pcm_ring_read(&ring,&speaker,copy,sizeof(copy),3,&info)&&info.sequence==2&&!info.skipped);
    for(unsigned i=3;i<=12;++i){memset(block,i,sizeof(block));assert(pcm_ring_push(&ring,block,sizeof(block),i*20000));}
    assert(pcm_ring_read(&ring,&speaker,copy,sizeof(copy),3,&info)&&info.sequence==12&&info.skipped==9&&copy[0]==12);
    assert(pcm_ring_read(&ring,&browser,copy,sizeof(copy),3,&info)&&info.sequence==12&&info.skipped==10);
    uint32_t before=ring.generation;pcm_ring_reset(&ring);
    assert(ring.generation!=before&&!pcm_ring_read(&ring,&speaker,copy,sizeof(copy),3,&info));
    assert(pcm_ring_push(&ring,block,sizeof(block),260000));
    assert(pcm_ring_read(&ring,&speaker,copy,sizeof(copy),3,&info)&&info.sequence==1&&!info.skipped);
    ring.generation=UINT32_MAX;pcm_ring_reset(&ring);assert(ring.generation==1);
    ring.sequence=UINT32_MAX;assert(pcm_ring_push(&ring,block,sizeof(block),1));
    speaker=(pcm_cursor_t){0};assert(pcm_ring_read(&ring,&speaker,copy,sizeof(copy),3,&info)&&info.sequence==0);
    uint8_t header[24];pcm_wire_header(header,&info);
    const uint8_t expected[24]={'P','C','M','1',0,0,0,0,0x80,0xbb,0,0,2,0,16,0,0,15,0,0,1,0,0,0};
    assert(!memcmp(header,expected,24));
    uint8_t stereo[]={0xff,0x7f,0x01,0x80,0,0x80,0,0x80,0xff,0x7f,0xff,0x7f,0,0,0,0x40};
    assert(pcm_stereo_to_dual_mono(stereo,sizeof(stereo)));
    const uint8_t mono[]={0,0,0,0,0,0x80,0,0x80,0xff,0x7f,0xff,0x7f,0,0x20,0,0x20};
    assert(!memcmp(stereo,mono,sizeof(mono))&&!pcm_stereo_to_dual_mono(stereo,3));
    unsigned volume;bool mute;
    assert(audio_parse_control("volume=100&mute=1",&volume,&mute)&&volume==100&&mute);
    assert(audio_parse_control("volume=0&mute=0",&volume,&mute)&&volume==0&&!mute);
    const char *bad[]={"volume=101&mute=0","volume=-1&mute=0","volume=12x&mute=0","volume=1&mute=00","volume=1&mute=2","volume=1&volume=2&mute=0","volume=%00&mute=1","volume=&mute=1","volume=99"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!audio_parse_control(bad[i],&volume,&mute));
    if(argc==2){
        FILE *file=fopen(argv[1],"wb");assert(file);
        info=(pcm_info_t){.sequence=7,.generation=3};pcm_wire_header(header,&info);
        memset(block,0,sizeof(block));block[0]=0xff;block[1]=0x7f;block[2]=0;block[3]=0x80;
        assert(fwrite(header,1,sizeof(header),file)==sizeof(header));
        assert(fwrite(block,1,sizeof(block),file)==sizeof(block));assert(fclose(file)==0);
    }
    free(storage);puts("PASS: PCM ring ownership, bounded backlog, reconnect/reset, wire header, mono mixing and audio controls");
}
