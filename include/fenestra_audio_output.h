#ifndef FENESTRA_AUDIO_OUTPUT_H
#define FENESTRA_AUDIO_OUTPUT_H
#include "fenestra_audio.h"
#include "fenestra_platform.h"
#include <stdatomic.h>

/* Trusted platform adapter API, not module callbacks. The adapter owns its
 * device, clocks, DMA buffers and cache synchronization. It installs a core-
 * resident output after resolving a verified native playback API, then
 * acquires a native session before consuming any queued module PCM.
 * No camera backend is implied by the presence of this software engine. */
typedef struct FxAudioStream FxAudioStream;
typedef struct {
    void *context;
    void *(*allocate)(void *,uint32_t);
    void (*release)(void *,void *);
    uint32_t sample_rate,max_submit_frames,max_queue_frames,max_render_frames,max_streams;
    uint64_t byte_budget;
    uint32_t output_channels; /* 0 preserves the default stereo adapter. */
} FxAudioOutputConfig;
typedef struct {
    FxAudioOutputConfig config;
    _Atomic uint32_t lock;
    FxAudioStream *head;
    uint64_t used_bytes;
    uint32_t streams,initialized;
} FxAudioOutput;

/* Firmware builds keep this root in the existing retained core heap state.
 * The standalone software fixture supplies its own zero-initialized root. */
typedef struct {
    _Atomic(FxAudioOutput *) output;
    _Atomic(void *) native;
    _Atomic uint32_t initializing;
} FxAudioDomain;
FxAudioDomain *fx_audio_domain(void);
typedef FxResult (*FxAudioRead)(void *,uint64_t,void *,uint32_t);

FxResult fx_audio_output_init(FxAudioOutput *,const FxAudioOutputConfig *);
/* One output, published once, retained until reboot. No hot replacement.
 * Registration does not touch hardware or start a task/interrupt. */
FxResult fx_audio_output_register(FxAudioOutput *);
FxAudioOutput *fx_audio_output_current(void);
FxResult fx_audio_info(FxAudioOutput *,FxAudioInfo *);
typedef struct {uint32_t streams,active;} FxAudioActivity;
FxResult fx_audio_activity(FxAudioOutput *,FxAudioActivity *);
FxResult fx_audio_stream_create(FxAudioOutput *,uint32_t owner,uint32_t generation,
                               const FxAudioConfig *,FxAudioStream **);
FxResult fx_audio_stream_write(FxAudioOutput *,FxAudioStream *,uint64_t samples,uint32_t frames,
                              FxAudioRead,void *context);
FxResult fx_audio_stream_control(FxAudioOutput *,FxAudioStream *,uint32_t op,uint32_t value);
FxResult fx_audio_stream_status(FxAudioOutput *,FxAudioStream *,FxAudioStatus *);
/* Revokes before trying the renderer lock. BUSY means retry; retain the stream
 * and cookie. OK proves no renderer still holds the stream or its PCM storage. */
FxResult fx_audio_stream_close(FxAudioOutput *,FxAudioStream *);
int fx_audio_stream_owner(const FxAudioStream *,uint32_t owner,uint32_t generation);
/* Adapter-owned stereo S16_LE destination, frames <= max_render_frames.
 * Suitable for a bounded render callback: no allocation, native calls, waits,
 * module callbacks, or borrowed module pointers. Lock contention returns BUSY
 * with silence; it consumes no queued samples. On OK every byte is initialized.
 * Hardware may still play an already rendered block after stream close. */
FxResult fx_audio_render(FxAudioOutput *,void *stereo_pcm,uint32_t frames);
/* Conservative adapter accounting: maximum queued source frames covered by
 * this block. Revocation may reduce actual consumption. Zero on contention or
 * no queued source; it does not inspect amplitude. */
FxResult fx_audio_render_counted(FxAudioOutput *,void *,uint32_t,uint32_t *source_frames);
FxResult fx_audio_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,
                           uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);
#endif
