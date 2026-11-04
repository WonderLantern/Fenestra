#include "fenestra_audio_output.h"
#include "fenestra_memory.h"

struct FxAudioStream {
    FxAudioStream *next;
    uint32_t owner,generation,allocation_bytes,channels,queue_frames;
    uint32_t read_frame,write_frame,queued_frames,state,gain_q16;
    uint64_t submitted_frames,rendered_frames,underruns;
    _Atomic uint32_t revoked;
    uint8_t pcm[];
};
static _Atomic(FxAudioOutput *) current;
static int take(FxAudioOutput *o) {
    uint32_t zero=0u;
    return atomic_compare_exchange_strong_explicit(&o->lock,&zero,1u,memory_order_acquire,memory_order_relaxed);
}
static void leave(FxAudioOutput *o) {atomic_store_explicit(&o->lock,0u,memory_order_release);}
static int member(FxAudioOutput *o,const FxAudioStream *s) {
    for(FxAudioStream *p=o->head;p;p=p->next)if(p==s)return 1;
    return 0;
}
FxResult fx_audio_output_init(FxAudioOutput *o,const FxAudioOutputConfig *c) {
    if(!o || !c || !c->allocate || !c->release || c->sample_rate<8000u || c->sample_rate>192000u ||
       !c->max_submit_frames || c->max_submit_frames>FX_AUDIO_MAX_SUBMIT_FRAMES ||
       !c->max_queue_frames || c->max_queue_frames>FX_AUDIO_MAX_QUEUE_FRAMES ||
       c->max_submit_frames>c->max_queue_frames || !c->max_render_frames ||
       c->max_render_frames>FX_AUDIO_MAX_QUEUE_FRAMES || !c->max_streams || !c->byte_budget)return FX_ARGUMENT;
    if(atomic_load_explicit(&current,memory_order_acquire)==o)return FX_STATE;
    memset(o,0,sizeof(*o));o->config=*c;atomic_init(&o->lock,0u);o->initialized=FX_AUDIO_ABI;return FX_OK;
}
FxResult fx_audio_output_register(FxAudioOutput *o) {
    if(!o || o->initialized!=FX_AUDIO_ABI)return FX_ARGUMENT;
    FxAudioOutput *expected=0;
    if(atomic_compare_exchange_strong_explicit(&current,&expected,o,memory_order_acq_rel,memory_order_acquire))return FX_OK;
    return expected==o?FX_OK:FX_BUSY;
}
FxAudioOutput *fx_audio_output_current(void) {return atomic_load_explicit(&current,memory_order_acquire);}
FxResult fx_audio_info(FxAudioOutput *o,FxAudioInfo *info) {
    if(!info)return FX_ARGUMENT;
    *info=(FxAudioInfo){.abi=FX_AUDIO_ABI,.bytes=sizeof(*info)};
    if(!o)return FX_OK; /* Explicit unavailable capability query, not silent playback. */
    if(o->initialized!=FX_AUDIO_ABI)return FX_STATE;
    if(!take(o))return FX_BUSY;
    info->capabilities=FX_AUDIO_CAP_OUTPUT|FX_AUDIO_CAP_MIX|FX_AUDIO_CAP_GAIN;
    info->sample_rate=o->config.sample_rate;info->output_channels=2u;info->format=FX_AUDIO_S16_LE;
    info->max_submit_frames=o->config.max_submit_frames;info->max_queue_frames=o->config.max_queue_frames;
    info->streams=o->streams;info->max_streams=o->config.max_streams;
    info->byte_budget=o->config.byte_budget;info->used_bytes=o->used_bytes;
    leave(o);return FX_OK;
}
FxResult fx_audio_stream_create(FxAudioOutput *o,uint32_t owner,uint32_t generation,
                               const FxAudioConfig *c,FxAudioStream **out) {
    if(!out || !c || !owner || !generation)return FX_ARGUMENT;
    *out=0;
    if(c->abi!=FX_AUDIO_ABI || c->bytes!=sizeof(*c) || c->reserved ||
       !c->queue_frames || c->queue_frames>FX_AUDIO_MAX_QUEUE_FRAMES || c->gain_q16>FX_AUDIO_GAIN_ONE)return FX_ARGUMENT;
    if(!o)return FX_CAPABILITY;
    if(o->initialized!=FX_AUDIO_ABI)return FX_STATE;
    if(c->sample_rate!=o->config.sample_rate || c->format!=FX_AUDIO_S16_LE ||
       (c->channels!=1u && c->channels!=2u))return FX_CAPABILITY;
    if(c->queue_frames>o->config.max_queue_frames)return FX_BOUNDS;
    uint32_t bytes=(uint32_t)sizeof(FxAudioStream)+c->queue_frames*c->channels*2u;
    if(!take(o))return FX_BUSY;
    if(bytes>o->config.byte_budget-o->used_bytes || o->streams>=o->config.max_streams){leave(o);return FX_BUDGET;}
    /* Allocation is worker-only. Render never waits for it. */
    FxAudioStream *s=o->config.allocate(o->config.context,bytes);
    if(!s){leave(o);return FX_BUDGET;}
    memset(s,0,sizeof(*s));atomic_init(&s->revoked,0u);
    s->owner=owner;s->generation=generation;s->allocation_bytes=bytes;s->channels=c->channels;
    s->queue_frames=c->queue_frames;s->gain_q16=c->gain_q16;s->state=FX_AUDIO_READY;
    s->next=o->head;o->head=s;o->used_bytes+=bytes;++o->streams;*out=s;
    leave(o);return FX_OK;
}
int fx_audio_stream_owner(const FxAudioStream *s,uint32_t owner,uint32_t generation) {
    return s && s->owner==owner && s->generation==generation;
}
FxResult fx_audio_stream_write(FxAudioOutput *o,FxAudioStream *s,uint64_t samples,uint32_t frames,
                              FxAudioRead read,void *context) {
    if(!o || !s || !read || !samples || !frames)return FX_ARGUMENT;
    if(frames>o->config.max_submit_frames)return FX_BOUNDS;
    if(!take(o))return FX_BUSY;
    if(!member(o,s)){leave(o);return FX_STALE;}
    if(atomic_load_explicit(&s->revoked,memory_order_acquire) ||
       s->state==FX_AUDIO_DRAINING || s->state==FX_AUDIO_DRAINED){leave(o);return FX_STATE;}
    if(frames>s->queue_frames){leave(o);return FX_BOUNDS;}
    if(frames>s->queue_frames-s->queued_frames){leave(o);return FX_BUSY;}
    if(s->submitted_frames>UINT64_MAX-frames){leave(o);return FX_BUDGET;}
    uint32_t stride=s->channels*2u,first=s->queue_frames-s->write_frame;
    if(first>frames)first=frames;
    uint32_t bytes=frames*stride;
    if(samples>UINT64_MAX-bytes){leave(o);return FX_BOUNDS;}
    FxResult result=read(context,samples,s->pcm+s->write_frame*stride,first*stride);
    if(result==FX_OK && first<frames)result=read(context,samples+first*stride,s->pcm,(frames-first)*stride);
    /* A failed second copy changed only free ring storage. No queue publication. */
    if(result==FX_OK) {
        s->write_frame=(s->write_frame+frames)%s->queue_frames;
        s->queued_frames+=frames;s->submitted_frames+=frames;
    }
    leave(o);return result;
}
FxResult fx_audio_stream_control(FxAudioOutput *o,FxAudioStream *s,uint32_t op,uint32_t value) {
    if(!o || !s || (op==FX_AUDIO_GAIN?value>FX_AUDIO_GAIN_ONE:value!=0u))return FX_ARGUMENT;
    if(!take(o))return FX_BUSY;
    if(!member(o,s)){leave(o);return FX_STALE;}
    if(atomic_load_explicit(&s->revoked,memory_order_acquire)){leave(o);return FX_STATE;}
    FxResult result=FX_OK;
    switch(op) {
        case FX_AUDIO_START:
            if(s->state==FX_AUDIO_DRAINING || s->state==FX_AUDIO_DRAINED)result=FX_STATE;
            else s->state=FX_AUDIO_PLAYING;
            break;
        case FX_AUDIO_PAUSE:
            if(s->state==FX_AUDIO_DRAINING || s->state==FX_AUDIO_DRAINED)result=FX_STATE;
            else s->state=FX_AUDIO_PAUSED;
            break;
        case FX_AUDIO_GAIN:s->gain_q16=value;break;
        case FX_AUDIO_DRAIN:
            s->state=s->queued_frames?FX_AUDIO_DRAINING:FX_AUDIO_DRAINED;
            result=s->queued_frames?FX_BUSY:FX_OK;break;
        case FX_AUDIO_STOP:
            s->state=FX_AUDIO_READY;s->queued_frames=0u;s->read_frame=0u;s->write_frame=0u;break;
        default:result=FX_ARGUMENT;break;
    }
    leave(o);return result;
}
FxResult fx_audio_stream_status(FxAudioOutput *o,FxAudioStream *s,FxAudioStatus *status) {
    if(!o || !s || !status)return FX_ARGUMENT;
    if(!take(o))return FX_BUSY;
    if(!member(o,s)){leave(o);return FX_STALE;}
    *status=(FxAudioStatus){FX_AUDIO_ABI,sizeof(*status),s->state,s->queued_frames,s->queue_frames,
        s->channels,s->gain_q16,0u,s->submitted_frames,s->rendered_frames,s->underruns};
    leave(o);return FX_OK;
}
FxResult fx_audio_stream_close(FxAudioOutput *o,FxAudioStream *s) {
    if(!o || !s)return FX_ARGUMENT;
    /* Only the owner worker closes. Its platform lease retains this allocation
     * across BUSY; the render callback observes revocation independently. */
    atomic_store_explicit(&s->revoked,1u,memory_order_release);
    if(!take(o))return FX_BUSY;
    FxAudioStream **link=&o->head;while(*link && *link!=s)link=&(*link)->next;
    if(!*link){leave(o);return FX_STALE;}
    *link=s->next;o->used_bytes-=s->allocation_bytes;--o->streams;
    leave(o);o->config.release(o->config.context,s);return FX_OK;
}
static int32_t sample(const uint8_t *p) {
    uint32_t v=(uint32_t)p[0]|((uint32_t)p[1]<<8);
    return v>=32768u?(int32_t)v-65536:(int32_t)v;
}
static void store(uint8_t *p,int64_t value) {
    if(value>32767)value=32767;
    if(value< -32768)value= -32768;
    uint16_t v=(uint16_t)value;p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
}
FxResult fx_audio_render(FxAudioOutput *o,void *pcm,uint32_t frames) {
    if(!o || !pcm || !frames || o->initialized!=FX_AUDIO_ABI)return FX_ARGUMENT;
    if(frames>o->config.max_render_frames)return FX_BOUNDS;
    uint8_t *out=pcm;memset(out,0,(size_t)frames*4u);
    if(!take(o))return FX_BUSY;
    for(FxAudioStream *s=o->head;s;s=s->next)
        if(!atomic_load_explicit(&s->revoked,memory_order_acquire) &&
           s->state==FX_AUDIO_PLAYING && s->queued_frames<frames && s->underruns!=UINT64_MAX)++s->underruns;
    for(uint32_t frame=0u;frame<frames;++frame) {
        int64_t left=0,right=0;
        for(FxAudioStream *s=o->head;s;s=s->next) {
            if(atomic_load_explicit(&s->revoked,memory_order_acquire) ||
               (s->state!=FX_AUDIO_PLAYING && s->state!=FX_AUDIO_DRAINING) || !s->queued_frames)continue;
            const uint8_t *p=s->pcm+s->read_frame*s->channels*2u;
            left+=(int64_t)sample(p)*s->gain_q16/(int64_t)FX_AUDIO_GAIN_ONE;
            right+=(int64_t)sample(s->channels==1u?p:p+2u)*s->gain_q16/(int64_t)FX_AUDIO_GAIN_ONE;
            s->read_frame=(s->read_frame+1u)%s->queue_frames;--s->queued_frames;++s->rendered_frames;
            if(!s->queued_frames && s->state==FX_AUDIO_DRAINING)s->state=FX_AUDIO_DRAINED;
        }
        store(out+frame*4u,left);store(out+frame*4u+2u,right);
    }
    leave(o);return FX_OK;
}
