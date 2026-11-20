/* Synthetic transport/clock; real PCM mixer and SoundService adapter. */
#include "fenestra_native_sound.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    FxNativeSound sound;FxAudioOutput output;FxSoundMessage *pending;
    uint32_t state,other,submissions,cleans;uintptr_t consumer;
    _Alignas(64) uint8_t pool[FX_SOUND_PERIOD_BYTES*2u];
    int16_t scratch[FX_SOUND_FRAMES*2u];
} Fixture;
static Fixture f;
static void *allocate(void *c,uint32_t n){(void)c;return malloc(n);}
static void release(void *c,void *p){(void)c;free(p);}
static void submit(void *c,FxSoundMessage *m) {
    Fixture *p=c;assert(!p->pending);p->pending=m;++p->submissions;
}
static uint32_t native_state(void *c){return ((Fixture *)c)->state;}
static int other(void *c){return ((Fixture *)c)->other!=0u;}
static uintptr_t consumer(void *c){return ((Fixture *)c)->consumer;}
static FxResult clean(void *c,void *p,uint32_t n) {
    Fixture *v=c;assert(n==FX_SOUND_PERIOD_BYTES && (p==v->pool || p==v->pool+FX_SOUND_PERIOD_BYTES));
    ++v->cleans;return FX_OK;
}
static void completed(uint32_t event,void *p){fx_sound_complete(&f.sound,event,p);}
static uint32_t word(const void *p){uint32_t v;memcpy(&v,p,4u);return v;}
static uint64_t wide(const void *p){uint64_t v;memcpy(&v,p,8u);return v;}
static uint32_t phase(void){return atomic_load(&f.sound.phase);}
static uint32_t command(void){assert(f.pending);return f.pending->command-f.sound.profile.command_base;}
static void init(int z9) {
    memset(&f,0,sizeof(f));
    FxAudioOutputConfig settings={.context=&f,.allocate=allocate,.release=release,.sample_rate=48000u,
        .max_submit_frames=16384u,.max_queue_frames=65536u,.max_render_frames=FX_SOUND_FRAMES,
        .max_streams=32u,.byte_budget=2097152u,.output_channels=1u};
    assert(fx_audio_output_init(&f.output,&settings)==FX_OK);
    FxSoundProfile p=z9?(FxSoundProfile){0x431d0014u,20u,20u,0u}:(FxSoundProfile){0x431d0010u,21u,16u,2u};
    FxSoundIO io={&f,submit,native_state,other,consumer,clean};
    assert(fx_sound_init(&f.sound,&p,&io,&f.output,f.scratch,completed)==FX_OK);
    FxAudioInfo info;assert(fx_audio_info(&f.output,&info)==FX_OK && info.output_channels==1u);
}
static FxResult read_pcm(void *c,uint64_t p,void *out,uint32_t n){(void)c;memcpy(out,(const void *)(uintptr_t)p,n);return FX_OK;}
static FxAudioStream *stream(uint32_t frames) {
    FxAudioStream *s=0;FxAudioConfig c={1u,sizeof(c),48000u,2u,FX_AUDIO_S16_LE,16384u,FX_AUDIO_GAIN_ONE,0u};
    assert(fx_audio_stream_create(&f.output,7u,3u,&c,&s)==FX_OK);
    static int16_t pcm[32768];for(uint32_t i=0u;i<frames;++i){pcm[i*2u]=3000;pcm[i*2u+1u]=1000;}
    assert(fx_audio_stream_write(&f.output,s,(uintptr_t)pcm,frames,read_pcm,0)==FX_OK);return s;
}
static void finish(int ok) {
    FxSoundMessage *m=f.pending;assert(m);f.pending=0;
    uint8_t *p=m->parameters;uint32_t op=m->command-f.sound.profile.command_base,result=0u;
    if(op==FX_SOUND_INIT) {
        assert(word(p)==1u && word(p+4u)==5u && word(p+8u)==0u && word(p+12u)==0u &&
               word(p+16u)==9600u && p[20]==f.sound.profile.init_extra);
        assert(f.state==0u);result=f.sound.profile.init_result;if(ok)f.state=1u;
    } else if(op==FX_SOUND_BUFFERS) {
        assert(f.state==1u);uint64_t address=(uintptr_t)f.pool;memcpy(p,&address,8u);
        uint32_t size=sizeof(f.pool);memcpy(p+8u,&size,4u);result=12u;
    } else if(op==FX_SOUND_PCM) {
        assert(f.state==1u && wide(p)==(uintptr_t)f.pool && word(p+8u)==9600u && word(p+12u)==2u && word(p+16u)==0u && p[20]==1u);
        result=21u;
    } else if(op==FX_SOUND_START) {
        assert(f.state==1u && word(p)==1u && wide(p+8u)==(uintptr_t)completed);
        result=f.sound.profile.start_result;if(ok){f.state=2u;f.consumer=(uintptr_t)completed;}
    } else if(op==FX_SOUND_STOP) {if(ok)f.state=0u;}
    else {assert(op==FX_SOUND_CLOSE && f.state==0u);}
    p[result]=(uint8_t)(ok!=0);m->callback(m->command,m->opaque);
}
static void prepared(void) {
    assert(fx_sound_request(&f.sound)==FX_BUSY);fx_sound_step(&f.sound);assert(command()==FX_SOUND_INIT);
    finish(1);fx_sound_step(&f.sound);assert(command()==FX_SOUND_BUFFERS);
    finish(1);fx_sound_step(&f.sound);assert(command()==FX_SOUND_PCM);
    finish(1);fx_sound_step(&f.sound);assert(phase()==FX_SOUND_READY && !f.pending);
    assert(fx_sound_request(&f.sound)==FX_OK);
}
static void start(FxAudioStream *s) {
    assert(fx_audio_stream_control(&f.output,s,FX_AUDIO_START,0u)==FX_OK);
    fx_sound_step(&f.sound);assert(command()==FX_SOUND_START && f.cleans==2u);
    finish(1);fx_sound_step(&f.sound);assert(phase()==FX_SOUND_RUNNING);
}
static void complete(uint32_t bank) {
    uint64_t payload[3]={(uintptr_t)(f.pool+bank*FX_SOUND_PERIOD_BYTES),9600u,0u};
    completed(f.sound.profile.command_base+FX_SOUND_COMPLETE,payload);
    memset(payload,0,sizeof(payload)); /* Original callback stack is already gone. */
}
static void stopped(void) {
    assert(command()==FX_SOUND_STOP && phase()==FX_SOUND_STOPPING);
    f.state=3u;fx_sound_step(&f.sound);assert(command()==FX_SOUND_STOP); /* No early close. */
    finish(1);fx_sound_step(&f.sound);assert(command()==FX_SOUND_CLOSE);
    finish(1);fx_sound_step(&f.sound);assert(!f.pending);
}
static void normal(void) {
    FxAudioStream *s=stream(12000u);prepared();start(s);
    const int16_t *p=(const int16_t *)f.pool;for(uint32_t i=0u;i<9600u;++i)assert(p[i]==2000);
    assert(fx_audio_stream_control(&f.output,s,FX_AUDIO_DRAIN,0u)==FX_BUSY);
    complete(0u);fx_sound_step(&f.sound);
    for(uint32_t i=0u;i<4800u;++i)assert(p[i]==(i<2400u?2000:0));
    assert(fx_audio_stream_control(&f.output,s,FX_AUDIO_DRAIN,0u)==FX_OK);
    assert(fx_audio_stream_close(&f.output,s)==FX_OK);
    fx_sound_step(&f.sound);assert(phase()==FX_SOUND_RUNNING); /* Hardware tail retained. */
    complete(1u);fx_sound_step(&f.sound);assert(phase()==FX_SOUND_RUNNING);
    complete(0u);fx_sound_step(&f.sound);stopped();assert(phase()==FX_SOUND_IDLE);
    s=stream(1u);prepared();assert(fx_audio_stream_close(&f.output,s)==FX_OK);
    fx_sound_step(&f.sound);stopped();assert(phase()==FX_SOUND_IDLE); /* Cancel before START. */
}
static void busy(void) {
    FxAudioStream *s=stream(12000u);f.state=2u;assert(fx_sound_request(&f.sound)==FX_BUSY);
    fx_sound_step(&f.sound);assert(!f.pending && !f.submissions);
    f.state=0u;f.other=1u;assert(fx_sound_request(&f.sound)==FX_BUSY);fx_sound_step(&f.sound);assert(!f.submissions);
    f.other=0u;prepared();start(s);f.consumer=1234u;
    fx_sound_step(&f.sound);assert(phase()==FX_SOUND_FAULT && !f.pending); /* Don't stop somebody else. */
    assert(fx_sound_request(&f.sound)==FX_STATE);assert(fx_audio_stream_close(&f.output,s)==FX_OK);
}
static void callbacks(void) {
    FxAudioStream *s=stream(12000u);prepared();start(s);uint32_t cleans=f.cleans;
    complete(0u);complete(1u);fx_sound_step(&f.sound);assert(f.cleans==cleans); /* Full ring missed. */
    stopped();assert(phase()==FX_SOUND_FAULT && fx_sound_request(&f.sound)==FX_IO);
    assert(fx_audio_stream_close(&f.output,s)==FX_OK);
}
static void timeout(void) {
    FxAudioStream *s=stream(1u);assert(fx_sound_request(&f.sound)==FX_BUSY);fx_sound_step(&f.sound);
    FxSoundMessage *held=f.pending;
    for(uint32_t i=0u;i<FX_SOUND_WAIT_TICKS;++i)fx_sound_step(&f.sound);
    assert(phase()==FX_SOUND_FAULT && held==f.pending && f.submissions==1u);
    assert(fx_sound_request(&f.sound)==FX_TIMEOUT);
    finish(1);fx_sound_step(&f.sound);assert(phase()==FX_SOUND_FAULT && !f.pending);
    assert(fx_audio_stream_close(&f.output,s)==FX_OK);
}
static void reject(void) {
    FxAudioStream *s=stream(1u);assert(fx_sound_request(&f.sound)==FX_BUSY);fx_sound_step(&f.sound);
    finish(1);fx_sound_step(&f.sound);assert(command()==FX_SOUND_BUFFERS);
    finish(0);fx_sound_step(&f.sound);assert(command()==FX_SOUND_STOP);
    finish(0);fx_sound_step(&f.sound);assert(phase()==FX_SOUND_FAULT && !f.pending);
    assert(fx_sound_request(&f.sound)==FX_IO);assert(fx_audio_stream_close(&f.output,s)==FX_OK);
}
int main(int argc,char **argv) {
    assert(argc==3);init(!strcmp(argv[1],"z9"));
    if(!strcmp(argv[2],"normal"))normal();
    else if(!strcmp(argv[2],"busy"))busy();
    else if(!strcmp(argv[2],"callbacks"))callbacks();
    else if(!strcmp(argv[2],"timeout"))timeout();
    else if(!strcmp(argv[2],"reject"))reject();
    else assert(0);
    printf("native sound %s %s passed (synthetic transport)\n",argv[1],argv[2]);return 0;
}
