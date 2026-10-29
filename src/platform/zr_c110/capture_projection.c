#include "fenestra_c110_capture.h"
/* Target adapter selects an output ABI; modules supply its complete finite
 * record transformation. No mode, geometry, rate or correction policy here. */
FxResult fx_c110_capture_project(const FxVideoConfigurationMode *m,uint32_t kind,void *out,uint32_t bytes) {
    if(!m)return FX_EMPTY;
    if(!kind || kind>FX_VIDEO_RECORDS)return FX_ARGUMENT;
    return fx_record_apply(&m->records[kind-1u],out,bytes);
}
