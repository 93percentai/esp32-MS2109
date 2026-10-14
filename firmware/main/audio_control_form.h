#pragma once
#include <stdbool.h>
#include "form_codec.h"
static inline bool audio_parse_control(const char *body,unsigned *volume,bool *muted)
{
    char percent[4],mute[2];
    if(!form_get(body,"volume",percent,sizeof(percent)) || !percent[0] ||
       !form_get(body,"mute",mute,sizeof(mute)) || (mute[0]!='0' && mute[0]!='1')) return false;
    unsigned value=0;
    for(unsigned i=0;percent[i];++i) {
        if(percent[i]<'0' || percent[i]>'9') return false;
        value=value*10+(percent[i]-'0');
    }
    if(value>100) return false;
    *volume=value;*muted=mute[0]=='1';return true;
}
