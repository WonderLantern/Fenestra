#ifndef FENESTRA_AUDIO_H
#define FENESTRA_AUDIO_H
#include "fenestra.h"

#define FX_AUDIO_ABI 1u
#define FX_AUDIO_OUTPUT_BINDING "camera.audio.output"
#define FX_AUDIO_GAIN_ONE 65536u
#define FX_AUDIO_MAX_QUEUE_FRAMES 65536u
#define FX_AUDIO_MAX_SUBMIT_FRAMES 16384u

/* PCM frames contain one signed little-endian 16-bit sample per channel.
 * Streams use the output's sample rate; resampling/decoding belongs in modules. */
enum {FX_AUDIO_S16_LE=1u};
enum {FX_AUDIO_CAP_OUTPUT=1u,FX_AUDIO_CAP_MIX=2u,FX_AUDIO_CAP_GAIN=4u};
enum {FX_AUDIO_QUERY,FX_AUDIO_CONFIGURE,FX_AUDIO_WRITE,FX_AUDIO_START,
      FX_AUDIO_PAUSE,FX_AUDIO_GAIN,FX_AUDIO_STATUS,FX_AUDIO_DRAIN,FX_AUDIO_STOP};
enum {FX_AUDIO_READY=1u,FX_AUDIO_PLAYING,FX_AUDIO_PAUSED,FX_AUDIO_DRAINING,FX_AUDIO_DRAINED};

typedef struct {
    uint32_t abi,bytes,capabilities,sample_rate,output_channels,format;
    uint32_t max_submit_frames,max_queue_frames,streams,max_streams;
    uint64_t byte_budget,used_bytes;
} FxAudioInfo;
typedef struct {
    uint32_t abi,bytes,sample_rate,channels,format,queue_frames,gain_q16,reserved;
} FxAudioConfig;
/* Worker copies this range from readable module image segments before replying.
 * Keep the source unchanged until the reply. Successful WRITE is all-or-nothing
 * queue acceptance, not confirmation of audible playback. */
typedef struct {uint64_t samples;uint32_t frames,reserved;} FxAudioWrite;
typedef struct {
    uint32_t abi,bytes,state,queued_frames,queue_frames,channels,gain_q16,reserved;
    uint64_t submitted_frames,rendered_frames,underruns;
} FxAudioStatus;
_Static_assert(sizeof(FxAudioInfo)==56u,"audio info wire ABI");
_Static_assert(sizeof(FxAudioConfig)==32u,"audio config wire ABI");
_Static_assert(sizeof(FxAudioWrite)==16u,"audio write wire ABI");
_Static_assert(sizeof(FxAudioStatus)==56u,"audio status wire ABI");
#endif
