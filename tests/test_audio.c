/* Synthetic PCM/worker fixtures. No Nikon device, firmware, clocks or DMA. */
#include "fenestra_audio_output.h"
#include "fenestra_audio_sdk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t allocations,releases,fail_allocation,reads,fail_read;
static FxAudioOutput output;
static FxAudioOutputConfig settings={0};
static void *allocate(void *context,uint32_t bytes) {
    (void)context;if(fail_allocation)return 0;
    void *p=malloc(bytes);if(p)++allocations;return p;
}
static void release(void *context,void *p) {(void)context;++releases;free(p);}
static FxResult read_pcm(void *context,uint64_t address,void *out,uint32_t bytes) {
    (void)context;++reads;if(fail_read && reads==fail_read)return FX_IO;
    memcpy(out,(const void *)(uintptr_t)address,bytes);return FX_OK;
}
static void init(void) {
    settings=(FxAudioOutputConfig){0,allocate,release,48000u,16384u,65536u,4096u,128u,1048576u};
    assert(fx_audio_output_init(&output,&settings)==FX_OK);
}
static FxAudioConfig config(uint32_t channels,uint32_t frames,uint32_t gain) {
    return (FxAudioConfig){FX_AUDIO_ABI,sizeof(FxAudioConfig),48000u,channels,FX_AUDIO_S16_LE,frames,gain,0u};
}
static FxAudioStream *stream(uint32_t channels,uint32_t frames,uint32_t gain) {
    FxAudioStream *s=0;FxAudioConfig c=config(channels,frames,gain);
    assert(fx_audio_stream_create(&output,7u,3u,&c,&s)==FX_OK && s);return s;
}
static void write_pcm(FxAudioStream *s,const int16_t *pcm,uint32_t frames) {
    assert(fx_audio_stream_write(&output,s,(uintptr_t)pcm,frames,read_pcm,0)==FX_OK);
}
static FxAudioStatus status(FxAudioStream *s) {
    FxAudioStatus value;assert(fx_audio_stream_status(&output,s,&value)==FX_OK);return value;
}
static void end(FxAudioStream *s) {assert(fx_audio_stream_close(&output,s)==FX_OK);}
static void test_mix(void) {
    init();FxAudioStream *mono=stream(1u,8u,FX_AUDIO_GAIN_ONE/2u),*stereo=stream(2u,8u,FX_AUDIO_GAIN_ONE);
    const int16_t m[]={1000,-1000,32767,-32768},s[]={2000,-2000,2000,-2000,32767,-32768,-32768,32767};
    int16_t out[8];write_pcm(mono,m,4u);write_pcm(stereo,s,4u);
    assert(fx_audio_stream_control(&output,mono,FX_AUDIO_START,0u)==FX_OK);
    assert(fx_audio_stream_control(&output,stereo,FX_AUDIO_START,0u)==FX_OK);
    assert(fx_audio_render(&output,out,4u)==FX_OK);
    const int16_t expected[]={2500,-1500,1500,-2500,32767,-16385,-32768,16383};
    assert(!memcmp(out,expected,sizeof(out)));
    assert(status(mono).underruns==0u && status(mono).rendered_frames==4u);
    assert(fx_audio_render(&output,out,4u)==FX_OK);
    for(uint32_t i=0u;i<8u;++i)assert(out[i]==0);
    assert(status(mono).underruns==1u);end(mono);end(stereo);
    assert(output.used_bytes==0u && allocations==releases);
}
static void test_queue(void) {
    init();FxAudioStream *s=stream(1u,4u,FX_AUDIO_GAIN_ONE);
    const int16_t a[]={1,2,3,4},b[]={5,6,7};int16_t out[8];
    write_pcm(s,a,4u);
    assert(fx_audio_stream_write(&output,s,(uintptr_t)b,1u,read_pcm,0)==FX_BUSY);
    assert(status(s).submitted_frames==4u);
    assert(fx_audio_stream_control(&output,s,FX_AUDIO_START,0u)==FX_OK);
    assert(fx_audio_render(&output,out,3u)==FX_OK && out[0]==1 && out[4]==3);
    write_pcm(s,b,3u); /* wraps at the physical ring boundary */
    assert(fx_audio_stream_control(&output,s,FX_AUDIO_PAUSE,0u)==FX_OK);
    assert(fx_audio_render(&output,out,1u)==FX_OK && out[0]==0 && status(s).queued_frames==4u);
    assert(fx_audio_stream_control(&output,s,FX_AUDIO_DRAIN,0u)==FX_BUSY);
    assert(fx_audio_stream_write(&output,s,(uintptr_t)a,1u,read_pcm,0)==FX_STATE);
    assert(fx_audio_render(&output,out,4u)==FX_OK);
    for(uint32_t i=0u;i<4u;++i)assert(out[i*2u]==(int16_t)(i+4u) && out[i*2u+1u]==out[i*2u]);
    assert(fx_audio_stream_control(&output,s,FX_AUDIO_DRAIN,0u)==FX_OK);
    assert(status(s).state==FX_AUDIO_DRAINED && status(s).underruns==0u);
    assert(fx_audio_stream_control(&output,s,FX_AUDIO_STOP,0u)==FX_OK);
    write_pcm(s,a,1u);assert(fx_audio_stream_control(&output,s,FX_AUDIO_GAIN,0u)==FX_OK);
    assert(fx_audio_stream_control(&output,s,FX_AUDIO_START,0u)==FX_OK);
    assert(fx_audio_render(&output,out,1u)==FX_OK && out[0]==0 && status(s).rendered_frames==8u);end(s);
}
static void test_failed_copy(void) {
    init();FxAudioStream *s=stream(1u,4u,FX_AUDIO_GAIN_ONE);const int16_t pcm[]={1,2,3};int16_t out[6];
    write_pcm(s,pcm,3u);assert(fx_audio_stream_control(&output,s,FX_AUDIO_START,0u)==FX_OK);
    assert(fx_audio_render(&output,out,2u)==FX_OK);
    reads=0u;fail_read=2u;
    assert(fx_audio_stream_write(&output,s,(uintptr_t)pcm,3u,read_pcm,0)==FX_IO);
    assert(status(s).queued_frames==1u && status(s).submitted_frames==3u);
    fail_read=0u;write_pcm(s,pcm,3u);
    assert(fx_audio_render(&output,out,3u)==FX_OK && out[0]==3 && out[2]==1 && out[4]==2);end(s);
}
static void test_limits(void) {
    init();FxAudioConfig c=config(1u,8u,FX_AUDIO_GAIN_ONE);FxAudioStream *s=0;
    c.sample_rate=44100u;assert(fx_audio_stream_create(&output,1u,1u,&c,&s)==FX_CAPABILITY && !s);
    c=config(3u,8u,FX_AUDIO_GAIN_ONE);assert(fx_audio_stream_create(&output,1u,1u,&c,&s)==FX_CAPABILITY);
    c=config(1u,8u,FX_AUDIO_GAIN_ONE+1u);assert(fx_audio_stream_create(&output,1u,1u,&c,&s)==FX_ARGUMENT);
    c=config(1u,8u,FX_AUDIO_GAIN_ONE);c.reserved=1u;
    assert(fx_audio_stream_create(&output,1u,1u,&c,&s)==FX_ARGUMENT);c.reserved=0u;
    output.config.byte_budget=1u;
    assert(fx_audio_stream_create(&output,1u,1u,&c,&s)==FX_BUDGET && !s);
    output.config.byte_budget=settings.byte_budget;fail_allocation=1u;
    assert(fx_audio_stream_create(&output,1u,1u,&c,&s)==FX_BUDGET && !s);
    fail_allocation=0u;s=stream(1u,8u,FX_AUDIO_GAIN_ONE);
    output.config.max_streams=1u;FxAudioStream *extra=0;
    assert(fx_audio_stream_create(&output,2u,1u,&c,&extra)==FX_BUDGET && !extra);
    assert(fx_audio_stream_owner(s,7u,3u) && !fx_audio_stream_owner(s,7u,4u));
    assert(fx_audio_stream_write(&output,s,UINT64_MAX-1u,2u,read_pcm,0)==FX_BOUNDS);
    assert(fx_audio_stream_write(&output,s,1u,9u,read_pcm,0)==FX_BOUNDS);
    assert(fx_audio_stream_control(&output,s,FX_AUDIO_GAIN,UINT32_MAX)==FX_ARGUMENT);end(s);
}
static void test_contention(void) {
    init();FxAudioStream *s=stream(1u,4u,FX_AUDIO_GAIN_ONE);const int16_t pcm[]={1000,2000};int16_t out[4];
    write_pcm(s,pcm,2u);assert(fx_audio_stream_control(&output,s,FX_AUDIO_START,0u)==FX_OK);
    atomic_store(&output.lock,1u);memset(out,0x7f,sizeof(out));
    assert(fx_audio_render(&output,out,2u)==FX_BUSY);
    for(uint32_t i=0u;i<4u;++i)assert(out[i]==0);
    assert(fx_audio_stream_close(&output,s)==FX_BUSY); /* revokes even while contended */
    assert(releases==0u);atomic_store(&output.lock,0u);
    assert(fx_audio_render(&output,out,2u)==FX_OK && out[0]==0 && status(s).queued_frames==2u);
    end(s);assert(allocations==releases);
}
static void test_many_streams(void) {
    init();FxAudioStream *voices[128];const int16_t pcm[]={1};int16_t out[2];
    for(uint32_t i=0u;i<128u;++i) {
        voices[i]=stream(1u,2u,FX_AUDIO_GAIN_ONE);write_pcm(voices[i],pcm,1u);
        assert(fx_audio_stream_control(&output,voices[i],FX_AUDIO_START,0u)==FX_OK);
    }
    FxAudioInfo info;assert(fx_audio_info(&output,&info)==FX_OK && info.streams==128u);
    assert(fx_audio_render(&output,out,1u)==FX_OK && out[0]==128 && out[1]==128);
    for(uint32_t i=0u;i<128u;++i)end(voices[i]);
    assert(output.streams==0u && output.used_bytes==0u && allocations==releases);
}

/* Real SDK -> service -> named platform binding -> audio provider -> renderer.
 * The synthetic profile substitutes only hardware/RTOS, with real module-range
 * validation and owner/generation/sequence/replay handling. */
typedef struct {
    FxPlatformOwner owner;
    FxServiceOwner service;
    FxServiceExchange exchange;
    FxServiceModule api;
    FxSdkClient sdk;
    uint64_t binding;
    uint32_t finished;
} Worker;
static FxResult route(void *context,const FxServiceRequest *q,FxServiceReply *a) {
    Worker *w=context;
    FxResult result=fx_platform_dispatch(&w->owner,q,a);
    if(result==FX_OK && q->operation==FX_S_LIFECYCLE && q->arg[0]==FX_LIFE_FINISH)w->finished=1u;
    if(result==FX_OK && q->operation==FX_S_PLATFORM && q->arg[0]==FX_BIND_OPEN)w->binding=a->value[0];
    return result;
}
static const FxNamedBinding audio_binding={
    FX_AUDIO_OUTPUT_BINDING,FX_BIND_DEVICE,FX_CALL_DEVICE,FX_AUDIO_ABI,FX_BIND_PROVIDER,FX_SDK_BINDING,0u,1u,{0}
};
static const FxPlatform platform={
    .abi=FX_PLATFORM_ABI,.count=1u,.model="synthetic",.firmware="audio-test",
    .bindings=&audio_binding,.io={.resource=fx_audio_resource}
};
static uint64_t cookie(Worker *w) {
    FxBindingLease *lease=fx_binding_store_lease(&w->owner.bindings,w->binding);
    return lease?lease->cookie:0u;
}
static void worker_init(Worker *w) {
    memset(w,0,sizeof(*w));
    assert(fx_platform_init(&w->owner,&platform,7u,3u,FX_SDK_BINDING|FX_SDK_QUERY)==FX_OK);
    fx_service_init(&w->service,7u,3u,FX_SDK_BINDING|FX_SDK_QUERY,(FxServiceBackend){w,route,0});
    w->api.service_abi=FX_SERVICE_ABI;w->api.generation=3u;w->api.exchange=&w->exchange;
    w->api.base.bytes=sizeof(w->api);
}
static FxServiceReply take_reply(Worker *w,FxResult expected) {
    assert(fx_service_dispatch(&w->service,&w->exchange.request,&w->exchange.reply)==expected);
    FxServiceReply reply;assert(fx_sdk_take(&w->sdk,&w->api,&reply)==FX_OK && reply.result==(uint32_t)expected);
    return reply;
}
static void module_pcm(Worker *w,void *samples,uint32_t bytes) {
    w->owner.module_image=samples;w->owner.module_bytes=bytes;w->owner.module_segment_count=1u;
    w->owner.module_segments[0]=(FxSegment){.memory_bytes=bytes,.flags=FX_READ|FX_WRITE};
}
static void test_worker(void) {
    Worker w;worker_init(&w);FxAudioInfo info;init();
    assert(fx_sdk_open(&w.sdk,&w.api,FX_AUDIO_OUTPUT_BINDING,1u)==FX_OK);(void)take_reply(&w,FX_OK);
    assert(fx_sdk_audio_query(&w.sdk,&w.api,w.binding)==FX_OK);
    FxServiceReply reply=take_reply(&w,FX_OK);memcpy(&info,reply.data,sizeof(info));
    assert(info.abi==1u && info.capabilities==0u); /* no output is registered yet */
    FxAudioConfig c=config(1u,8u,FX_AUDIO_GAIN_ONE);
    assert(fx_sdk_audio_configure(&w.sdk,&w.api,w.binding,&c)==FX_OK);(void)take_reply(&w,FX_CAPABILITY);
    assert(fx_audio_output_register(&output)==FX_OK);
    assert(fx_audio_output_register(&output)==FX_OK);
    assert(fx_audio_output_init(&output,&settings)==FX_STATE);
    assert(fx_sdk_audio_configure(&w.sdk,&w.api,w.binding,&c)==FX_OK);(void)take_reply(&w,FX_OK);
    int16_t pcm[]={123,-456};module_pcm(&w,pcm,sizeof(pcm));
    assert(fx_sdk_audio_write(&w.sdk,&w.api,w.binding,pcm,2u)==FX_OK);(void)take_reply(&w,FX_OK);
    /* Exact request replay returns its cached result without writing twice. */
    assert(fx_service_dispatch(&w.service,&w.exchange.request,&w.exchange.reply)==FX_OK);
    assert(fx_sdk_audio_write(&w.sdk,&w.api,w.binding,pcm,3u)==FX_OK);(void)take_reply(&w,FX_BOUNDS);
    assert(fx_sdk_audio_control(&w.sdk,&w.api,w.binding,FX_AUDIO_STATUS,0u)==FX_OK);
    reply=take_reply(&w,FX_OK);FxAudioStatus st;memcpy(&st,reply.data,sizeof(st));
    assert(st.queued_frames==2u && st.submitted_frames==2u);
    assert(fx_sdk_audio_control(&w.sdk,&w.api,w.binding,FX_AUDIO_START,0u)==FX_OK);(void)take_reply(&w,FX_OK);
    int16_t out[4];assert(fx_audio_render(&output,out,2u)==FX_OK && out[0]==123 && out[2]==-456);
    FxBindingLease *lease=fx_binding_store_lease(&w.owner.bindings,w.binding);assert(lease);
    ++w.owner.generation;
    assert(fx_audio_resource(0,FX_RESOURCE_PANIC,0,&w.owner,w.binding,&lease->cookie,0,0)==FX_STALE);--w.owner.generation;
    atomic_store(&output.lock,1u);
    assert(fx_platform_stop(&w.owner,1u)==FX_BUSY && cookie(&w));
    atomic_store(&output.lock,0u);
    assert(fx_platform_stop(&w.owner,1u)==FX_OK && !cookie(&w));
    assert(allocations==releases);
}
int fenestra_module_entry(const FxModuleServices *);
static void test_tone(int available) {
    Worker w;worker_init(&w);init();if(available)assert(fx_audio_output_register(&output)==FX_OK);
    uint64_t nonzero=0u;
    for(uint32_t tick=0u;tick<2000u && !w.finished;++tick) {
        assert(fenestra_module_entry(&w.api.base)==0);
        FxServiceRequest *q=&w.exchange.request;
        if(q->sequence>w.service.last_sequence) {
            if(q->operation==FX_S_PLATFORM && q->arg[0]==FX_BIND_CALL && q->arg[2]==FX_AUDIO_WRITE) {
                FxAudioWrite write;memcpy(&write,q->data,sizeof(write));
                module_pcm(&w,(void *)(uintptr_t)write.samples,write.frames*2u);
            }
            (void)fx_service_dispatch(&w.service,q,&w.exchange.reply);
        }
        int16_t pcm[256];assert(fx_audio_render(&output,pcm,128u)==FX_OK);
        for(uint32_t i=0u;i<256u;++i)if(pcm[i])++nonzero;
    }
    assert(w.finished && !cookie(&w) && output.used_bytes==0u && allocations==releases);
    assert(available?nonzero>20000u:nonzero==0u);
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"mix"))test_mix();
    else if(!strcmp(argv[1],"queue"))test_queue();
    else if(!strcmp(argv[1],"failed-copy"))test_failed_copy();
    else if(!strcmp(argv[1],"limits"))test_limits();
    else if(!strcmp(argv[1],"contention"))test_contention();
    else if(!strcmp(argv[1],"many-streams"))test_many_streams();
    else if(!strcmp(argv[1],"worker"))test_worker();
    else if(!strcmp(argv[1],"tone"))test_tone(1);
    else if(!strcmp(argv[1],"unavailable"))test_tone(0);
    else assert(0);
    printf("audio %s passed (synthetic PCM, no physical output)\n",argv[1]);return 0;
}
