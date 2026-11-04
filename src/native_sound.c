#include "fenestra_native_sound.h"
#include "fenestra_memory.h"

enum {INIT,BUFFERS,PCM,START,STOP,CLOSE};
static const uint32_t offsets[]={FX_SOUND_INIT,FX_SOUND_BUFFERS,FX_SOUND_PCM,FX_SOUND_START,FX_SOUND_STOP,FX_SOUND_CLOSE};
static void put32(void *p,uint32_t value){memcpy(p,&value,sizeof(value));}
static void put64(void *p,uint64_t value){memcpy(p,&value,sizeof(value));}
static uint32_t get32(const void *p){uint32_t value;memcpy(&value,p,sizeof(value));return value;}
static uint64_t get64(const void *p){uint64_t value;memcpy(&value,p,sizeof(value));return value;}
static void acknowledge(uint32_t event,void *opaque) {
    FxSoundRequest *r=opaque;
    r->unexpected=event!=r->message.command;
    atomic_store_explicit(&r->done,1u,memory_order_release);
}
static void phase(FxNativeSound *s,uint32_t value) {
    s->age=0u;atomic_store_explicit(&s->phase,value,memory_order_release);
}
static void fault(FxNativeSound *s,FxResult reason) {
    atomic_store_explicit(&s->accept_pcm,0u,memory_order_release);
    atomic_store_explicit(&s->failure,(uint32_t)reason,memory_order_release);
    phase(s,FX_SOUND_FAULT); /* Outstanding original pointers remain valid. */
}
static void send(FxNativeSound *s,uint32_t which,uint32_t next) {
    FxSoundRequest *r=&s->requests[which];r->unexpected=0u;
    atomic_store_explicit(&r->done,0u,memory_order_relaxed);
    phase(s,next);s->io.submit(s->io.context,&r->message);
}
static uint32_t result_offset(const FxNativeSound *s,uint32_t which) {
    if(which==INIT)return s->profile.init_result;
    if(which==START)return s->profile.start_result;
    if(which==BUFFERS)return 12u;
    if(which==PCM)return 21u;
    return 0u;
}
/* 0 pending, 1 accepted, -1 rejected, -2 timed out. Only the original callback
 * authorizes reading its result. Command records never change command identity. */
static int accepted(FxNativeSound *s,uint32_t which) {
    FxSoundRequest *r=&s->requests[which];
    if(!atomic_load_explicit(&r->done,memory_order_acquire))
        return ++s->age>=FX_SOUND_WAIT_TICKS?-2:0;
    return !r->unexpected && r->parameters[result_offset(s,which)]?1:-1;
}
FxResult fx_sound_init(FxNativeSound *s,const FxSoundProfile *profile,const FxSoundIO *io,
    FxAudioOutput *output,int16_t scratch[FX_SOUND_FRAMES*2u],FxSoundCallback callback) {
    if(!s || !profile || !io || !io->submit || !io->state || !io->other_active || !io->consumer ||
       !io->clean || !output || !scratch || !callback || output->config.sample_rate!=48000u ||
       output->config.max_render_frames<FX_SOUND_FRAMES ||
       (profile->command_base!=UINT32_C(0x431d0010) && profile->command_base!=UINT32_C(0x431d0014)) ||
       profile->init_result>23u || profile->start_result>23u)return FX_ARGUMENT;
    memset(s,0,sizeof(*s));s->profile=*profile;s->io=*io;s->output=output;s->scratch=scratch;s->callback=callback;
    atomic_init(&s->phase,FX_SOUND_IDLE);atomic_init(&s->wanted,0u);atomic_init(&s->failure,FX_OK);
    atomic_init(&s->accept_pcm,0u);atomic_init(&s->completed,0u);atomic_init(&s->callback_error,0u);
    for(uint32_t i=0u;i<6u;++i) {
        FxSoundRequest *r=&s->requests[i];atomic_init(&r->done,0u);
        r->message.callback=acknowledge;r->message.command=profile->command_base+offsets[i];
        r->message.parameters=r->parameters;r->message.opaque=r;
    }
    return FX_OK;
}
FxResult fx_sound_request(FxNativeSound *s) {
    if(!s)return FX_ARGUMENT;
    uint32_t p=atomic_load_explicit(&s->phase,memory_order_acquire);
    if(p==FX_SOUND_FAULT)return (FxResult)atomic_load_explicit(&s->failure,memory_order_acquire);
    if(p==FX_SOUND_READY || p==FX_SOUND_RUNNING)return FX_OK;
    if(p==FX_SOUND_IDLE && (s->io.state(s->io.context) || s->io.other_active(s->io.context)))return FX_BUSY;
    atomic_store_explicit(&s->wanted,1u,memory_order_release);return FX_BUSY;
}
void fx_sound_complete(FxNativeSound *s,uint32_t event,const void *payload) {
    if(!s || !atomic_load_explicit(&s->accept_pcm,memory_order_acquire))return;
    if(!payload || event!=s->profile.command_base+FX_SOUND_COMPLETE) {
        atomic_store_explicit(&s->callback_error,1u,memory_order_release);return;
    }
    /* Native completion payload is stack storage. Copy just the bank pointer;
     * the original WAV player also refills its fixed 9600-byte period. */
    uint64_t address=get64(payload),base=(uintptr_t)s->pool;
    uint32_t mask=address==base?1u:address==base+FX_SOUND_PERIOD_BYTES?2u:0u;
    if(!mask || (atomic_fetch_or_explicit(&s->completed,mask,memory_order_acq_rel)&mask))
        atomic_store_explicit(&s->callback_error,1u,memory_order_release);
}
static FxResult fill(FxNativeSound *s,uint32_t bank) {
    uint32_t source=0u;
    FxResult r=fx_audio_render_counted(s->output,s->scratch,FX_SOUND_FRAMES,&source);
    if(r!=FX_OK && r!=FX_BUSY)return r;
    uint8_t *destination=s->pool+bank*FX_SOUND_PERIOD_BYTES;
    for(uint32_t i=0u;i<FX_SOUND_FRAMES;++i) {
        int32_t mono=((int32_t)s->scratch[i*2u]+s->scratch[i*2u+1u])/2;
        uint16_t bits=(uint16_t)mono;destination[i*2u]=(uint8_t)bits;destination[i*2u+1u]=(uint8_t)(bits>>8);
    }
    s->bank_data[bank]=source!=0u;
    return s->io.clean(s->io.context,destination,FX_SOUND_PERIOD_BYTES);
}
static void stop(FxNativeSound *s,FxResult reason) {
    if(reason!=FX_OK)atomic_store_explicit(&s->failure,(uint32_t)reason,memory_order_release);
    uint32_t native=s->io.state(s->io.context);
    if((native==2u && s->io.consumer(s->io.context)!=(uintptr_t)s->callback) || native>2u) {
        fault(s,reason==FX_OK?FX_STATE:reason);return;
    }
    atomic_store_explicit(&s->accept_pcm,0u,memory_order_release);
    memset(s->requests[STOP].parameters,0,32u);
    send(s,STOP,FX_SOUND_STOPPING);
}
void fx_sound_step(FxNativeSound *s) {
    if(!s)return;
    uint32_t p=atomic_load_explicit(&s->phase,memory_order_acquire);
    if(p==FX_SOUND_FAULT)return;
    FxAudioActivity activity;
    if(fx_audio_activity(s->output,&activity)!=FX_OK)return;
    if(p==FX_SOUND_IDLE) {
        if(!activity.streams){atomic_store_explicit(&s->wanted,0u,memory_order_release);return;}
        if(!atomic_load_explicit(&s->wanted,memory_order_acquire) || s->io.state(s->io.context) ||
           s->io.other_active(s->io.context))return;
        uint8_t *a=s->requests[INIT].parameters;memset(a,0,32u);
        put32(a,1u);put32(a+4u,5u);put32(a+16u,FX_SOUND_PERIOD_BYTES);a[20]=(uint8_t)s->profile.init_extra;
        s->ready_age=0u;s->bank_data[0]=s->bank_data[1]=0u;
        atomic_store_explicit(&s->completed,0u,memory_order_relaxed);
        atomic_store_explicit(&s->callback_error,0u,memory_order_relaxed);
        send(s,INIT,FX_SOUND_INITIALIZING);return;
    }
    if(p==FX_SOUND_READY) {
        if(!activity.streams || (!activity.active && ++s->ready_age>=FX_SOUND_WAIT_TICKS)) {stop(s,FX_OK);return;}
        if(!activity.active)return;
        if(s->io.state(s->io.context)!=1u){fault(s,FX_STATE);return;}
        FxResult r=fill(s,0u);if(r==FX_OK)r=fill(s,1u);
        if(r!=FX_OK){stop(s,r);return;}
        uint8_t *a=s->requests[START].parameters;memset(a,0,32u);put32(a,1u);put64(a+8u,(uintptr_t)s->callback);
        atomic_store_explicit(&s->accept_pcm,1u,memory_order_release);
        send(s,START,FX_SOUND_STARTING);return;
    }
    if(p==FX_SOUND_RUNNING) {
        if(s->io.state(s->io.context)!=2u || s->io.consumer(s->io.context)!=(uintptr_t)s->callback) {
            fault(s,FX_STATE);return; /* Never stop or overwrite a foreign player. */
        }
        uint32_t completed=atomic_exchange_explicit(&s->completed,0u,memory_order_acq_rel);
        if(atomic_load_explicit(&s->callback_error,memory_order_acquire) || completed==3u) {
            stop(s,FX_IO);return; /* Missed a full ring: no writes into a live bank. */
        }
        if(completed) {
            FxResult r=fill(s,completed==1u?0u:1u);
            if(r!=FX_OK){stop(s,r);return;}
            s->age=0u;
        } else if(++s->age>=FX_SOUND_WAIT_TICKS){stop(s,FX_TIMEOUT);return;}
        /* A software DRAIN/CLOSE can precede audible completion. Keep both
         * native banks until their completion notifications actually arrive. */
        if(!activity.active && !s->bank_data[0] && !s->bank_data[1])stop(s,FX_OK);
        return;
    }
    uint32_t which=p==FX_SOUND_INITIALIZING?INIT:p==FX_SOUND_GETTING_BUFFERS?BUFFERS:
        p==FX_SOUND_SETTING_PCM?PCM:p==FX_SOUND_STARTING?START:p==FX_SOUND_STOPPING?STOP:CLOSE;
    int ack=accepted(s,which);if(!ack)return;
    if(ack<0) {
        FxResult reason=ack==-2?FX_TIMEOUT:FX_IO;
        if(ack==-1 && (which==BUFFERS || which==PCM || which==START))stop(s,reason);
        else fault(s,reason);
        return;
    }
    if(which==INIT) {
        memset(s->requests[BUFFERS].parameters,0,32u);
        send(s,BUFFERS,FX_SOUND_GETTING_BUFFERS);
    } else if(which==BUFFERS) {
        const uint8_t *a=s->requests[BUFFERS].parameters;uint64_t base=get64(a);
        if(!base || (base&15u) || base>UINTPTR_MAX-FX_SOUND_PERIOD_BYTES*2u || get32(a+8u)<FX_SOUND_PERIOD_BYTES*2u) {
            stop(s,FX_BOUNDS);return;
        }
        s->pool=(uint8_t *)(uintptr_t)base;
        uint8_t *b=s->requests[PCM].parameters;memset(b,0,32u);
        put64(b,base);put32(b+8u,FX_SOUND_PERIOD_BYTES);put32(b+12u,2u);b[20]=1u;
        send(s,PCM,FX_SOUND_SETTING_PCM);
    } else if(which==PCM)phase(s,FX_SOUND_READY);
    else if(which==START) {atomic_store_explicit(&s->wanted,0u,memory_order_release);phase(s,FX_SOUND_RUNNING);}
    else if(which==STOP) {
        if(s->io.state(s->io.context)!=0u){fault(s,FX_STATE);return;}
        memset(s->requests[CLOSE].parameters,0,32u);send(s,CLOSE,FX_SOUND_CLOSING);
    } else {
        s->pool=0;s->bank_data[0]=s->bank_data[1]=0u;
        atomic_store_explicit(&s->wanted,0u,memory_order_release);
        phase(s,atomic_load_explicit(&s->failure,memory_order_acquire)==FX_OK?FX_SOUND_IDLE:FX_SOUND_FAULT);
    }
}
