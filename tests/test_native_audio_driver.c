/* Real ordinary-module/SDK/provider/native-worker path. Only RTOS scheduling,
 * SoundService transport and cache/physical device behavior are synthetic. */
#include "fenestra_native_sound.h"
#include "fenestra_audio_sdk.h"
#include "fenestra_machine.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint32_t attributes,reserved;uintptr_t context,entry;
    uint32_t priority,reserved2;uint64_t stack_bytes;uintptr_t stack;
    uint32_t processor,affinity;
} Task;
static FxAudioDomain domain;
static Task task;
static jmp_buf scheduler;
static uint32_t allocated,freed,creates,resumes,waits,native_state,commands,cleans,closed,finished;
static uintptr_t native_consumer;
static FxSoundMessage *pending;
static _Alignas(64) uint8_t pool[19200];
static FxPlatformOwner owner;
static FxServiceOwner service;
static FxServiceExchange exchange;
static FxServiceModule api;
FxAudioDomain *fx_audio_domain(void){return &domain;}
static void *allocate(uint32_t bytes,uint32_t flags) {
    assert(!flags);void *p=malloc(bytes);assert(p);++allocated;return p;
}
static void release(void *p){assert(p);++freed;free(p);}
static int32_t create(const Task *t) {
    assert(!creates && t->entry && t->context && !(t->stack&15u) && t->stack_bytes==16384u &&
           t->processor==1u && t->affinity==UINT32_MAX);
    task=*t;++creates;return 7;
}
static int32_t resume(int32_t id){assert(id==7);++resumes;return 0;}
static int32_t delay(uint32_t ticks){assert(ticks==1u);++waits;longjmp(scheduler,1);}
static uint32_t state(void){return native_state;}
static uint32_t idle(void){return 0u;}
static void submit(FxSoundMessage *m){assert(!pending);pending=m;++commands;}
static FxResult clean(void *context,uint64_t address,uint32_t bytes,uint32_t operation) {
    (void)context;assert((address==(uintptr_t)pool || address==(uintptr_t)pool+9600u) && bytes==9600u && operation==FX_MACHINE_CLEAN);
    ++cleans;return FX_OK;
}
#define FUNCTION(name,fn) {name,FX_BIND_FUNCTION,FX_CALL_U64_8,1u,FX_BIND_ESCAPE,FX_SDK_NATIVE,0u,(uintptr_t)fn,{0}}
static const FxNamedBinding bindings[]={
    {FX_AUDIO_OUTPUT_BINDING,FX_BIND_DEVICE,FX_CALL_DEVICE,1u,FX_BIND_PROVIDER,FX_SDK_BINDING,0u,1u,{0}},
    FUNCTION("rtos.memory.allocate",allocate),FUNCTION("rtos.memory.release",release),
    FUNCTION("rtos.task.create",create),FUNCTION("rtos.task.resume",resume),FUNCTION("rtos.task.delay",delay),
    FUNCTION("camera.audio.queue.submit",submit),FUNCTION("camera.audio.playback.state",state),
    FUNCTION("camera.audio.recording.state",idle),FUNCTION("camera.audio.beep.state",idle),
    {"camera.audio.playback.consumer",FX_BIND_TABLE,FX_CALL_BYTES,1u,FX_BIND_READ,FX_SDK_BINDING,8u,(uintptr_t)&native_consumer,{0}}
};
static const FxPlatform platform={.abi=1u,.count=sizeof(bindings)/sizeof(bindings[0]),
    .model="synthetic audio transport",.firmware="fixture",.bindings=bindings,
    .io={.cache=clean,.resource=fx_audio_resource}};
static FxResult route(void *context,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;
    FxResult r=fx_platform_dispatch(&owner,q,a);
    if(r==FX_OK && q->operation==FX_S_LIFECYCLE && q->arg[0]==FX_LIFE_FINISH)finished=1u;
    return r;
}
static uint64_t wide(const void *p){uint64_t n;memcpy(&n,p,8u);return n;}
static void native_finish(void) {
    FxSoundMessage *m=pending;if(!m)return;pending=0;
    uint8_t *p=m->parameters;uint32_t op=m->command-0x431d0010u,result=0u;
    if(op==FX_SOUND_INIT){assert(native_state==0u);native_state=1u;result=21u;}
    else if(op==FX_SOUND_BUFFERS) {
        assert(native_state==1u);uint64_t address=(uintptr_t)pool;uint32_t size=sizeof(pool);
        memcpy(p,&address,8u);memcpy(p+8u,&size,4u);result=12u;
    } else if(op==FX_SOUND_PCM){assert(native_state==1u && wide(p)==(uintptr_t)pool);result=21u;}
    else if(op==FX_SOUND_START){assert(native_state==1u);native_state=2u;native_consumer=(uintptr_t)wide(p+8u);result=16u;}
    else if(op==FX_SOUND_STOP){assert(native_state==1u || native_state==2u);native_state=0u;}
    else {assert(op==FX_SOUND_CLOSE && native_state==0u);++closed;}
    p[result]=1u;m->callback(m->command,m->opaque);
}
static void worker_step(void) {
    if(!resumes)return;
    if(!setjmp(scheduler))((void (*)(void *))task.entry)((void *)task.context);
}
int fenestra_module_entry(const FxModuleServices *);
int main(void) {
    assert(fx_platform_init(&owner,&platform,7u,3u,FX_SDK_QUERY|FX_SDK_BINDING)==FX_OK);
    fx_service_init(&service,7u,3u,FX_SDK_QUERY|FX_SDK_BINDING,(FxServiceBackend){0,route,0});
    api.service_abi=FX_SERVICE_ABI;api.generation=3u;api.exchange=&exchange;api.base.bytes=sizeof(api);
    assert(!allocated && !commands && !creates && !fx_audio_output_current());
    uint32_t playback_ticks=0u,bank=0u;uint64_t played_samples=0u;
    for(uint32_t tick=0u;tick<3000u && !(finished && closed);++tick) {
        if(!finished)assert(fenestra_module_entry(&api.base)==0);
        FxServiceRequest *q=&exchange.request;
        if(q->sequence>service.last_sequence) {
            if(q->operation==FX_S_PLATFORM && q->arg[0]==FX_BIND_CALL && q->arg[2]==FX_AUDIO_WRITE) {
                FxAudioWrite w;memcpy(&w,q->data,sizeof(w));
                owner.module_image=(void *)(uintptr_t)w.samples;owner.module_bytes=w.frames*2u;owner.module_segment_count=1u;
                owner.module_segments[0]=(FxSegment){.memory_bytes=owner.module_bytes,.flags=FX_READ|FX_WRITE};
            }
            (void)fx_service_dispatch(&service,q,&exchange.reply);
            if(q->operation==FX_S_PLATFORM && q->arg[0]==FX_BIND_CALL && q->arg[2]==FX_AUDIO_QUERY) {
                FxAudioInfo info;memcpy(&info,exchange.reply.data,sizeof(info));
                assert(info.output_channels==1u && info.sample_rate==48000u);
                assert(allocated==1u && !commands && !creates); /* Heap metadata only. */
            }
        }
        native_finish();worker_step();
        if(native_state==2u && ++playback_ticks%10u==0u) {
            const int16_t *pcm=(const int16_t *)(pool+bank*9600u);
            for(uint32_t i=0u;i<4800u;++i)if(pcm[i])++played_samples;
            uint64_t transient[3]={(uintptr_t)(pool+bank*9600u),9600u,0u};
            ((FxSoundCallback)native_consumer)(0x431d0017u,transient);bank^=1u;
        }
    }
    assert(finished && closed==1u && creates==1u && resumes==1u && waits>0u);
    assert(played_samples>11000u && played_samples<=12000u && cleans>=5u);
    assert(!pending && native_state==0u && allocated==freed+3u); /* Resident metadata, stack, scratch. */
    puts("ordinary tone -> real SDK/provider/core worker -> native PCM buffers passed (synthetic hardware)");return 0;
}
