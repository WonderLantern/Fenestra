#ifndef FENESTRA_C110_CAPTURE_H
#define FENESTRA_C110_CAPTURE_H
#include "fenestra_video_configuration.h"
/* Output adapters for independently fingerprinted C1.10 functions. The caller
 * must hold the original pipeline's configuration lock and a live mode lease.
 * These functions neither acquire that lease nor install runtime hooks. */
enum {
    FX_C110_SOURCE=1, FX_C110_GEOMETRY, FX_C110_CPC_SIZE,
    FX_C110_RAW_SOURCE_SIZE, FX_C110_RAW_STORED_SIZE,
    FX_C110_ACTIVE_AREA, FX_C110_NRMT_HEIGHT, FX_C110_R3D_RECORD,
    FX_C110_PREVIEW_PROFILE, FX_C110_PREVIEW_DIMENSIONS,
    FX_C110_PICTURE_SIZE, FX_C110_NCTG_HEIGHT
};
/* A NULL profile is the stock path. A mismatched output remains byte-for-byte
 * untouched. Input/output must be kernel-owned copies, never module pointers. */
FxResult fx_c110_capture_project(const FxVideoConfigurationMode *,uint32_t,void *,uint32_t);
#endif
