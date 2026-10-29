#ifndef FENESTRA_VIDEO_SERVICE_H
#define FENESTRA_VIDEO_SERVICE_H
#include "fenestra.h"

/* A typed request is copied by the trusted owner after the module yields.
 * No module-supplied pointer, native address or callback enters this API. */
typedef struct {
    uint32_t generation, expected_revision;
    FxVideoRequest mode;
} FxVideoChange;
typedef struct {
    uint32_t revision, recording;
    FxVideoRequest mode;
} FxVideoSnapshot;

/* Platform contract, not a Nikon binding. begin holds exclusive pipeline
 * ownership until finish; read/drain/apply are bounded and serialized there.
 * drain succeeds only after all capture/DMA users have acknowledged stopping.
 * apply never resumes capture. finish(valid=0) must keep capture stopped.
 * The owner serializes runtime state. Callback-boundary revocation blocks
 * subsequent writes and valid finalization; finish must itself keep capture
 * stopped if it detects a failure/revocation during its final commit.
 * These guarantees require independent qualification on the target firmware.
 * An absent adapter or incomplete capabilities cannot perform a write. */
typedef struct {
    void *context;
    FxVideoCaps capabilities;
    FxResult (*begin)(void *,uint64_t *,FxVideoSnapshot *);
    FxResult (*drain)(void *,uint64_t);
    FxResult (*apply)(void *,uint64_t,const FxVideoRequest *);
    FxResult (*read)(void *,uint64_t,FxVideoSnapshot *);
    FxResult (*finish)(void *,uint64_t,uint32_t valid);
} FxVideoBackend;
typedef struct {
    FxRuntime *runtime;
    FxVideoBackend backend;
    uint32_t busy,quarantined;
} FxVideoService;
typedef struct {
    FxResult result,rollback,finish;
    uint32_t applied,rolled_back,quarantined;
    FxVideoSnapshot before,after;
    FxVideoPlan plan;
} FxVideoReceipt;

FxResult fx_video_change(FxVideoService *,const FxVideoChange *,FxVideoReceipt *);
#endif
