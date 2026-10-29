#ifndef FENESTRA_C110_CAPTURE_HOOKS_H
#define FENESTRA_C110_CAPTURE_HOOKS_H
#include "fenestra_c110_capture.h"
#include <stdatomic.h>

/* Owned RAM, immutable projections after prepare. Only the original pipeline
 * owner may publish mode after its drain boundary, and clear it after drain.
 * A module request/boolean is not permission to call set_mode. These adapters
 * deliberately do not expose an activation function to EL0 modules. */
typedef struct {
    FxCaptureProfile profiles[FX_VIDEO_MAX_MODES];
    FxVideoConfigurationData *configuration; /* Allocated only on a module BEGIN, never boot. */
    uint8_t large[FX_VIDEO_MAX_MODES][2][376],small[FX_VIDEO_MAX_MODES][2][56];
    uint16_t picture[FX_VIDEO_MAX_MODES][2];
    uintptr_t large_source[FX_VIDEO_MAX_MODES][2],small_source[FX_VIDEO_MAX_MODES][2],picture_source[FX_VIDEO_MAX_MODES];
    _Atomic uint32_t mode,fault,projected,prepared;
    /* Platform-owned stopped/released transaction; never supplied by EL0. */
    int (*handoff)(void);
    /* Read-only prospective display aspect, while Nikon builds its new view
     * before the RAW owner's stopped callback. Never applies to DMA/buffers,
     * live frame geometry or file metadata. */
    int (*display_mode)(uint32_t *mode);
} FxC110CaptureHooks;

/* The platform owner supplies this accessor. NULL preserves stock behavior. */
FxC110CaptureHooks *fx_c110_capture_hooks(void);
FxResult fx_c110_capture_hooks_prepare(FxC110CaptureHooks *,const void *,uint32_t bytes);
uint64_t fx_c110_capture_source(uint64_t,const uint32_t *,void *);
uint64_t fx_c110_capture_geometry(const uint32_t *,void *);
uint64_t fx_c110_capture_cpc(const uint32_t *,void *);
uint64_t fx_c110_capture_raw_source(const uint32_t *,void *);
uint64_t fx_c110_capture_raw_stored(const uint32_t *,void *);
uint64_t fx_c110_capture_active(const uint32_t *,void *);
uint64_t fx_c110_capture_nrmt(const uint32_t *,void *);
uint64_t fx_c110_capture_file(const uint32_t *,void *);
uint64_t fx_c110_capture_movie_metadata(const uint32_t *,void *);
const void *fx_c110_capture_large(const uint32_t *);
const void *fx_c110_capture_small(const uint32_t *);
const void *fx_c110_capture_picture(uint32_t,uint32_t);
uint64_t fx_c110_capture_picture_copy(uint32_t,uint32_t,void *);
uint64_t fx_c110_capture_sensor(const uint32_t *);
uint64_t fx_c110_capture_pool(const uint32_t *,uint32_t);
#endif
