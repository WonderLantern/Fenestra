#ifndef FENESTRA_NATIVE_SOUND_H
#define FENESTRA_NATIVE_SOUND_H
#include "fenestra_audio_output.h"

/* Core-only adapter for the original SoundService playback queue. Never a
 * module ABI. Message/parameter/callback storage remains resident until reboot. */
#define FX_SOUND_FRAMES 4800u
#define FX_SOUND_PERIOD_BYTES (FX_SOUND_FRAMES * 2u)
#define FX_SOUND_WAIT_TICKS 500u
typedef void (*FxSoundCallback)(uint32_t,void *);
typedef struct {
    uintptr_t links[3];
    FxSoundCallback callback;
    uint32_t command,reserved;
    void *parameters,*opaque;
} FxSoundMessage;
_Static_assert(sizeof(FxSoundMessage)==56 && offsetof(FxSoundMessage,callback)==24 &&
    offsetof(FxSoundMessage,parameters)==40,"original SoundService queue record");

typedef struct {uint32_t command_base,init_result,start_result,init_extra;} FxSoundProfile;
enum {FX_SOUND_INIT,FX_SOUND_START,FX_SOUND_STOP,FX_SOUND_SET_CONFIG,FX_SOUND_GET_CONFIG,
      FX_SOUND_BUFFERS,FX_SOUND_PCM,FX_SOUND_COMPLETE,FX_SOUND_STOP_COMPLETE,FX_SOUND_CLOSE};
enum {FX_SOUND_IDLE,FX_SOUND_INITIALIZING,FX_SOUND_GETTING_BUFFERS,FX_SOUND_SETTING_PCM,
      FX_SOUND_READY,FX_SOUND_STARTING,FX_SOUND_RUNNING,FX_SOUND_STOPPING,FX_SOUND_CLOSING,FX_SOUND_FAULT};
typedef struct {
    void *context;
    void (*submit)(void *,FxSoundMessage *);
    uint32_t (*state)(void *);
    int (*other_active)(void *);
    uintptr_t (*consumer)(void *);
    FxResult (*clean)(void *,void *,uint32_t);
} FxSoundIO;
typedef struct {
    FxSoundMessage message;
    _Alignas(8) uint8_t parameters[32];
    _Atomic uint32_t done;
    uint32_t unexpected;
} FxSoundRequest;
typedef struct {
    FxSoundProfile profile;FxSoundIO io;FxAudioOutput *output;int16_t *scratch;
    FxSoundCallback callback;
    FxSoundRequest requests[6]; /* INIT, BUFFERS, PCM, START, STOP, CLOSE. */
    _Atomic uint32_t phase,wanted,failure,accept_pcm,completed,callback_error;
    uint8_t *pool;uint32_t bank_data[2],age,ready_age;
} FxNativeSound;

FxResult fx_sound_init(FxNativeSound *,const FxSoundProfile *,const FxSoundIO *,
    FxAudioOutput *,int16_t stereo_scratch[FX_SOUND_FRAMES*2u],FxSoundCallback);
/* Request/complete may run concurrently with the sole normal-task step caller.
 * Completion copies the transient native payload and never renders or waits. */
FxResult fx_sound_request(FxNativeSound *);
void fx_sound_complete(FxNativeSound *,uint32_t event,const void *);
void fx_sound_step(FxNativeSound *);
FxResult fx_native_audio_register(FxPlatformOwner *);
FxResult fx_native_audio_prepare(void);
#endif
