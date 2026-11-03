#ifndef FENESTRA_AUDIO_H
#define FENESTRA_AUDIO_H
#include "fenestra_extensions.h"
/* Provider contract; original audio bindings and device policy live in a
 * native extension. Core neither claims a physical codec nor starts audio. */
#define FX_AUDIO_ABI 1u
enum {
    FX_AUDIO_QUERY,
    FX_AUDIO_OPEN,
    FX_AUDIO_START,
    FX_AUDIO_STOP,
    FX_AUDIO_STATUS,
    FX_AUDIO_CONTROL,
    FX_AUDIO_CLOSE
};
enum { FX_AUDIO_CAPTURE = 1u, FX_AUDIO_PLAYBACK = 2u, FX_AUDIO_MONITOR = 4u, FX_AUDIO_SYNTHETIC = 8u };
enum { FX_AUDIO_S16 = 1u, FX_AUDIO_S24 = 2u, FX_AUDIO_S32 = 3u, FX_AUDIO_F32 = 4u };
enum {
    FX_AUDIO_IDLE,
    FX_AUDIO_OPENED,
    FX_AUDIO_RUNNING,
    FX_AUDIO_STOPPED,
    FX_AUDIO_FAILED,
    FX_AUDIO_OPENING,
    FX_AUDIO_STARTING,
    FX_AUDIO_STOPPING,
    FX_AUDIO_CLOSING
};
typedef struct {
    uint32_t abi, bytes, operations, directions, formats, rate_min, rate_max, channels_max;
    uint32_t period_min, period_max, flags, reserved;
    char name[64];
} FxAudioInfo;
typedef struct {
    uint32_t rate, channels, format, period_frames;
} FxAudioFormat;
typedef struct {
    uint64_t stream;
    uint32_t direction, reserved;
    FxAudioFormat format;
} FxAudioOpen;
typedef struct {
    uint64_t frames, overruns, underruns, timestamp;
    uint32_t state, error;
    FxAudioFormat format;
} FxAudioStatus;
typedef struct {
    uint32_t key, index;
    int64_t value;
} FxAudioControl;
typedef struct {
    void *context;
    FxResult (*query)(void *, FxAudioInfo *);
    FxResult (*open)(void *, const FxAudioOpen *, FxStream *);
    FxResult (*start)(void *);
    FxResult (*stop)(void *);
    FxResult (*close)(void *);
    FxResult (*status)(void *, FxAudioStatus *);
    FxResult (*control)(void *, const FxAudioControl *);
} FxAudioDriver;
typedef struct {
    FxAudioDriver driver;
    void *lookup_context;
    FxResult (*stream_lookup)(void *, uint64_t, FxStreamInfo *);
    uint32_t owner, generation;
    FxStream *stream;
    FxAudioFormat format;
    FxAudioOpen request;
    uint32_t state;
    _Atomic uint32_t lock;
} FxAudioDevice;
FxResult fx_audio_provider(void *, const FxProviderClient *, uint64_t, const void *, uint32_t,
                           FxServiceReply *);
uint32_t fx_audio_sample_bytes(uint32_t format);
#endif
