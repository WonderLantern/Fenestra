#include "fenestra_native_sound.h"
#include "fenestra_machine.h"
#include "fenestra_memory.h"
#include "audio_profile.h"

/* One lazy core task; no module image, owner, RTOS task or native DMA buffer
 * becomes its lifetime anchor. The platform itself is immutable/resident. */
typedef struct {
    uint32_t attributes,reserved;uintptr_t context,entry;
    uint32_t priority,reserved2;uint64_t stack_bytes;uintptr_t stack;
    uint32_t processor,affinity;
} SoundTask;
_Static_assert(sizeof(SoundTask)==56 && offsetof(SoundTask,entry)==16 &&
    offsetof(SoundTask,stack_bytes)==32 && offsetof(SoundTask,affinity)==52,"original task ABI");
typedef struct {
    FxAudioOutput output;FxNativeSound sound;
    const FxPlatform *platform;
    uintptr_t allocate,release,submit,state,recording,beep,consumer,create,resume,delay;
    void *stack,*scratch;
    _Atomic uint32_t task;
} NativeAudio;
#define FN(value,type) ((type)(uintptr_t)(value))
static void *allocate(void *context,uint32_t bytes) {
    NativeAudio *d=context;return FN(d->allocate,void *(*)(uint32_t,uint32_t))(bytes,0u);
}
static void release(void *context,void *p) {
    NativeAudio *d=context;FN(d->release,void (*)(void *))(p);
}
static void submit(void *context,FxSoundMessage *message) {
    NativeAudio *d=context;FN(d->submit,void (*)(FxSoundMessage *))(message);
}
static uint32_t state(void *context) {
    NativeAudio *d=context;return FN(d->state,uint32_t (*)(void))();
}
static int other_active(void *context) {
    NativeAudio *d=context;
    return FN(d->recording,uint32_t (*)(void))()!=0u || FN(d->beep,uint32_t (*)(void))()!=0u;
}
static uintptr_t consumer(void *context) {
    NativeAudio *d=context;
    uintptr_t p=*(const volatile uintptr_t *)d->consumer;
    atomic_thread_fence(memory_order_acquire);return p;
}
static FxResult clean(void *context,void *address,uint32_t bytes) {
    NativeAudio *d=context;
    return d->platform->io.cache(d->platform->io.context,(uintptr_t)address,bytes,FX_MACHINE_CLEAN);
}
static void completed(uint32_t event,void *payload) {
    FxAudioDomain *root=fx_audio_domain();
    NativeAudio *d=root?atomic_load_explicit(&root->native,memory_order_acquire):0;
    if(d)fx_sound_complete(&d->sound,event,payload);
}
static void worker(void *context) {
    NativeAudio *d=context;
    for(;;) {
        fx_sound_step(&d->sound);
        /* Render on this task, never in AudioIF's IRQ or SoundService's task.
         * The original player also queues completion before refilling. */
        if(FN(d->delay,int32_t (*)(uint32_t))(1u)) {
            atomic_store_explicit(&d->sound.failure,FX_IO,memory_order_release);
            atomic_store_explicit(&d->sound.phase,FX_SOUND_FAULT,memory_order_release);
            return; /* Core task/storage and any native references are retained. */
        }
    }
}
static uintptr_t function(FxPlatformOwner *owner,const char *name) {
    const FxNamedBinding *b=fx_platform_find(owner,name);
    return b && b->kind==FX_BIND_FUNCTION && b->abi==FX_CALL_U64_8?b->address:0u;
}
FxResult fx_native_audio_register(FxPlatformOwner *owner) {
    if(!owner || !owner->platform)return FX_ARGUMENT;
    FxAudioDomain *root=fx_audio_domain();if(!root)return FX_STATE;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong_explicit(&root->initializing,&zero,1u,memory_order_acq_rel,memory_order_acquire))
        return zero==2u?FX_OK:zero==1u?FX_BUSY:FX_CAPABILITY;
    uintptr_t allocator=function(owner,"rtos.memory.allocate"),freer=function(owner,"rtos.memory.release");
    if(!allocator || !freer) {
        atomic_store_explicit(&root->initializing,3u,memory_order_release);return FX_CAPABILITY;
    }
    NativeAudio *d=FN(allocator,void *(*)(uint32_t,uint32_t))((uint32_t)sizeof(*d),0u);
    if(!d){atomic_store_explicit(&root->initializing,0u,memory_order_release);return FX_BUDGET;}
    memset(d,0,sizeof(*d));atomic_init(&d->task,0u);
    d->platform=owner->platform;d->allocate=allocator;d->release=freer;
    d->submit=function(owner,"camera.audio.queue.submit");d->state=function(owner,"camera.audio.playback.state");
    d->recording=function(owner,"camera.audio.recording.state");d->beep=function(owner,"camera.audio.beep.state");
    d->create=function(owner,"rtos.task.create");d->resume=function(owner,"rtos.task.resume");d->delay=function(owner,"rtos.task.delay");
    const FxNamedBinding *b=fx_platform_find(owner,"camera.audio.playback.consumer");
    d->consumer=b && b->kind==FX_BIND_TABLE && b->bytes==sizeof(uintptr_t)?b->address:0u;
    if(!d->allocate || !d->release || !d->submit || !d->state || !d->recording || !d->beep ||
       !d->consumer || !d->create || !d->resume || !d->delay || !d->platform->io.cache) {
        release(d,d);atomic_store_explicit(&root->initializing,3u,memory_order_release);return FX_CAPABILITY;
    }
    FxAudioOutputConfig config={.context=d,.allocate=allocate,.release=release,.sample_rate=48000u,
        .max_submit_frames=16384u,.max_queue_frames=65536u,.max_render_frames=FX_SOUND_FRAMES,
        .max_streams=32u,.byte_budget=2097152u,.output_channels=1u};
    FxResult r=fx_audio_output_init(&d->output,&config);
    if(r==FX_OK)r=fx_audio_output_register(&d->output);
    if(r==FX_OK)atomic_store_explicit(&root->native,d,memory_order_release);
    else release(d,d);
    atomic_store_explicit(&root->initializing,r==FX_OK?2u:3u,memory_order_release);return r;
}
FxResult fx_native_audio_prepare(void) {
    FxAudioDomain *root=fx_audio_domain();if(!root)return FX_STATE;
    NativeAudio *d=atomic_load_explicit(&root->native,memory_order_acquire);
    if(!d)return atomic_load_explicit(&root->initializing,memory_order_acquire)==1u?FX_BUSY:FX_CAPABILITY;
    uint32_t zero=0u;
    if(atomic_compare_exchange_strong_explicit(&d->task,&zero,1u,memory_order_acq_rel,memory_order_acquire)) {
        d->stack=allocate(d,16384u);d->scratch=allocate(d,FX_SOUND_FRAMES*4u);
        if(!d->stack || !d->scratch || ((uintptr_t)d->stack&15u)) {
            if(d->stack)release(d,d->stack);
            if(d->scratch)release(d,d->scratch);
            d->stack=d->scratch=0;atomic_store_explicit(&d->task,3u,memory_order_release);return FX_BUDGET;
        }
        FxSoundProfile profile=FX_TARGET_SOUND_PROFILE;
        FxSoundIO io={d,submit,state,other_active,consumer,clean};
        FxResult r=fx_sound_init(&d->sound,&profile,&io,&d->output,d->scratch,completed);
        if(r!=FX_OK){atomic_store_explicit(&d->task,3u,memory_order_release);return r;}
        SoundTask task={0u,0u,(uintptr_t)d,(uintptr_t)worker,28u,0u,16384u,(uintptr_t)d->stack,1u,UINT32_MAX};
        int32_t id=FN(d->create,int32_t (*)(const SoundTask *))(&task);
        if(id<1 || FN(d->resume,int32_t (*)(int32_t))(id)) {
            /* A created RTOS task can retain its stack even if resume fails. */
            atomic_store_explicit(&d->task,3u,memory_order_release);return FX_IO;
        }
        atomic_store_explicit(&d->task,2u,memory_order_release);
    } else if(zero!=2u)return zero==1u?FX_BUSY:FX_IO;
    return fx_sound_request(&d->sound);
}
