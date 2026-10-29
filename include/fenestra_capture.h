#ifndef FENESTRA_CAPTURE_H
#define FENESTRA_CAPTURE_H
#include "fenestra.h"
/* Separate domains: sensor envelope, stored frame, active picture, preview,
 * uncompressed CPC work and compressed RAW pool are not interchangeable. */
typedef struct { uint32_t width,height; } FxCaptureSize;
typedef struct { uint32_t x,y,width,height; } FxCaptureRect;
typedef struct {
    uint32_t abi,bytes,mode_id,codec;
    FxCaptureSize source,stored;
    FxCaptureRect active_source,active_stored;
    uint32_t receiver_x,receiver_y,fps_num,fps_den;
    uint32_t cpc_pitch,cpc_frame_bytes;
    uint32_t raw_unit_bytes,raw_count;
    uint64_t raw_total_bytes,raw_region_capacity,file_bytes_per_second;
    FxCaptureSize preview;
    FxCaptureRect preview_source;
    uint32_t sensor_mode,sensor_profile,flags,reserved;
} FxCaptureProfile;
enum { FX_CAPTURE_ABI=1,FX_CAPTURE_CODEC_R3D=1 };
enum {
    FX_CAPTURE_SENSOR=1u, FX_CAPTURE_GEOMETRY=2u, FX_CAPTURE_CPC=4u,
    FX_CAPTURE_RAW_POOL=8u, FX_CAPTURE_R3D=16u, FX_CAPTURE_PREVIEW=32u,
    FX_CAPTURE_MENU=64u, FX_CAPTURE_DRAIN=128u, FX_CAPTURE_RESTORE=256u,
    FX_CAPTURE_STOP=512u, FX_CAPTURE_FRAME_METADATA=1024u,
    FX_CAPTURE_REQUIRED=2047u
};
/* Arithmetic and coherence only. Never a substitute for native qualification. */
FxResult fx_capture_profile_check(const FxCaptureProfile *);
#endif
