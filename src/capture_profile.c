#include "fenestra_capture.h"
static int inside(FxCaptureRect r,FxCaptureSize s) {
    return r.width && r.height && r.x<=s.width && r.y<=s.height &&
        r.width<=s.width-r.x && r.height<=s.height-r.y;
}
FxResult fx_capture_profile_check(const FxCaptureProfile *p) {
    if(!p || p->abi!=FX_CAPTURE_ABI || p->bytes!=sizeof(*p) || !p->mode_id ||
       !p->codec || p->reserved || p->flags)return FX_ARGUMENT;
    if(!inside(p->active_source,p->source) || !inside(p->active_stored,p->stored) ||
       !inside(p->preview_source,p->source) || !p->fps_num || !p->fps_den ||
       !p->preview.width || !p->preview.height || !p->raw_unit_bytes || !p->raw_count ||
       !p->cpc_pitch || !p->raw_region_capacity)return FX_BOUNDS;
    if(p->active_source.width!=p->active_stored.width || p->active_source.height!=p->active_stored.height ||
       p->receiver_x>p->active_source.x || p->receiver_y>p->active_source.y ||
       p->active_source.x-p->receiver_x!=p->active_stored.x ||
       p->active_source.y-p->receiver_y!=p->active_stored.y ||
       p->receiver_x>p->source.width || p->receiver_y>p->source.height ||
       p->stored.width>p->source.width-p->receiver_x ||
       p->stored.height>p->source.height-p->receiver_y)return FX_INTEGRITY;
    if(p->preview_source.x!=p->active_source.x || p->preview_source.y!=p->active_source.y ||
       p->preview_source.width!=p->active_source.width || p->preview_source.height!=p->active_source.height ||
       (uint64_t)p->preview.width*p->active_source.height!=(uint64_t)p->preview.height*p->active_source.width)
        return FX_INTEGRITY;
    if((uint64_t)p->cpc_pitch*p->source.height!=p->cpc_frame_bytes ||
       (uint64_t)p->raw_unit_bytes*p->raw_count!=p->raw_total_bytes ||
       p->raw_total_bytes>p->raw_region_capacity || p->raw_unit_bytes%512u ||
       p->raw_unit_bytes<=1024u || !p->file_bytes_per_second)return FX_BUDGET;
    return FX_OK;
}
