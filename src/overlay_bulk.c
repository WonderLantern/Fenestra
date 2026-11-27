#include "fenestra_overlay_bulk.h"

static FxOverlayVersion *writing(FxOverlayBulk *job) {
    if(job->actor.generation!=job->store->generation)return 0;
    for(uint32_t i=0;i<FX_OVERLAY_VERSIONS;++i) {
        FxOverlayVersion *v=&job->store->versions[i];
        if(v->id==job->stage && v->owner==job->actor.owner && v->state==FX_OVERLAY_WRITING)return v;
    }
    return 0;
}
static FxResult failed(FxOverlayBulk *job,FxResult reason) {
    job->phase=FX_OVERLAY_BULK_FAILED;return fx_overlay_panic(job->store,reason);
}
static FxResult close_source(FxOverlayBulk *job) {
    if(job->close_attempted)return FX_STATE;
    job->close_attempted=1u;
    FxResult r=job->source.close(job->source.context);
    if(r!=FX_OK)return failed(job,r);
    job->closed=1u;
    if(job->store->phase!=FX_OVERLAY_ACTIVE) {job->phase=FX_OVERLAY_BULK_FAILED;return FX_STATE;}
    return FX_OK;
}
/* The source uses synchronous reads. A known-owned file can close cleanly
 * after a read or integrity error. Keep the previously published version. */
static FxResult reject(FxOverlayBulk *job,FxResult reason) {
    if(!job->closed) {
        const FxResult closed=close_source(job);
        if(closed!=FX_OK)return closed;
    }
    job->phase=FX_OVERLAY_BULK_FAILED;
    const FxResult result=fx_overlay_abort(job->store,job->actor,job->stage);
    return result==FX_OK?reason:result;
}

FxResult fx_overlay_bulk_init(FxOverlayBulk *job,FxOverlay *s,FxOverlayActor a,uint64_t stage,FxOverlaySource source) {
    if(!job || !s || !a.owner || !stage || !source.read || !source.close)return FX_ARGUMENT;
    *job=(FxOverlayBulk){.store=s,.actor=a,.stage=stage,.source=source};
    if(s->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    return writing(job)?FX_OK:FX_STALE;
}
FxResult fx_overlay_bulk_step(FxOverlayBulk *job) {
    if(!job || !job->store)return FX_ARGUMENT;
    if(job->phase!=FX_OVERLAY_BULK_READING || job->store->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    FxOverlayVersion *v=writing(job);if(!v)return FX_STALE;
    uint32_t remaining=v->bytes-v->written,requested=remaining,done=0u;
    uint8_t extra=0u;
    if(requested>FX_OVERLAY_BULK_CHUNK)requested=FX_OVERLAY_BULK_CHUNK;
    FxResult r=job->source.read(job->source.context,remaining?v->allocation.data+v->written:&extra,
                               remaining?requested:1u,&done);
    if(job->store->phase!=FX_OVERLAY_ACTIVE) {job->phase=FX_OVERLAY_BULK_FAILED;return FX_STATE;}
    if(r!=FX_OK)return reject(job,r);
    if(!remaining) {
        if(done)return reject(job,FX_BOUNDS);
        r=close_source(job);if(r!=FX_OK)return r;
        r=fx_overlay_seal(job->store,job->actor,job->stage);
        if(r!=FX_OK)return reject(job,r);
        job->phase=FX_OVERLAY_BULK_READY;
        return FX_OK;
    }
    if(!done || done>requested)return reject(job,FX_BOUNDS);
    r=fx_sha256_update(&v->hash,v->allocation.data+v->written,done);
    if(r!=FX_OK)return failed(job,r);
    v->written+=done;return FX_BUSY;
}
FxResult fx_overlay_bulk_cancel(FxOverlayBulk *job) {
    if(!job || !job->store)return FX_ARGUMENT;
    if(job->store->phase!=FX_OVERLAY_ACTIVE || job->phase==FX_OVERLAY_BULK_CANCELLED ||
       job->phase==FX_OVERLAY_BULK_FAILED)return FX_STATE;
    if(!job->closed) {
        FxResult r=close_source(job);if(r!=FX_OK)return r;
    }
    FxResult r=fx_overlay_abort(job->store,job->actor,job->stage);
    if(r==FX_OK)job->phase=FX_OVERLAY_BULK_CANCELLED;
    return r;
}
