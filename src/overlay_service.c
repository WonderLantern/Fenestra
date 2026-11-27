#include "fenestra_extension_services.h"
#include "fenestra_memory.h"

FxResult fx_overlay_service_stop(void *context,uint32_t panic) {
    FxOverlayService *s=context;if(!s || !s->store)return FX_ARGUMENT;
    if(panic) {(void)fx_overlay_panic(s->store,FX_STATE);return FX_OK;}
    return fx_overlay_stop(s->store);
}

static FxOverlayTarget *owned_target(FxOverlayService *s,uint64_t id) {
    if(id>UINT32_MAX)return 0;
    for(uint32_t i=0;i<s->store->target_count;++i) {
        FxOverlayTarget *t=&s->store->targets[i];
        if(t->spec.id==id && t->spec.publisher==s->actor.owner)return t;
    }
    return 0;
}
FxResult fx_overlay_service_call(void *context,const FxServiceRequest *q,FxServiceReply *a) {
    FxOverlayService *s=context;
    if(!s || !s->store || !q || !a)return FX_ARGUMENT;
    if(q->operation!=FX_S_SUBSTITUTION)return FX_CAPABILITY;
    if(q->generation!=s->actor.generation || s->actor.generation!=s->store->generation)return FX_STALE;
    if(q->bytes>FX_SERVICE_BYTES)return FX_BOUNDS;
    switch(q->arg[0]) {
    case FX_SUBST_QUERY:
        if(q->bytes || q->arg[1] || q->arg[2] || q->arg[3])return FX_ARGUMENT;
        a->value[0]=FX_OVERLAY_ABI;a->value[1]=s->store->phase;
        a->value[2]=FX_OVERLAY_CHUNK;a->value[3]=s->store->retained;return FX_OK;
    case FX_SUBST_BEGIN: {
        FxOverlayTarget *t=owned_target(s,q->arg[1]);if(!t)return FX_CAPABILITY;
        if(q->bytes!=96u || q->arg[2]>UINT32_MAX || q->arg[3]>UINT32_MAX)return FX_ARGUMENT;
        if(memcmp(q->data+64u,t->spec.schema_sha256,32u))return FX_INTEGRITY;
        uint64_t id=0;
        FxResult r=fx_overlay_begin(s->store,s->actor,t->spec.id,(uint32_t)q->arg[2],(uint32_t)q->arg[3],q->data,q->data+32u,&id);
        a->value[0]=id;return r;
    }
    case FX_SUBST_FEED:
        if(q->arg[2]>UINT32_MAX || q->arg[3])return FX_ARGUMENT;
        return fx_overlay_feed(s->store,s->actor,q->arg[1],(uint32_t)q->arg[2],q->data,q->bytes);
    case FX_SUBST_SEAL:case FX_SUBST_ABORT:
        if(q->bytes || q->arg[2] || q->arg[3])return FX_ARGUMENT;
        return q->arg[0]==FX_SUBST_SEAL?fx_overlay_seal(s->store,s->actor,q->arg[1]):fx_overlay_abort(s->store,s->actor,q->arg[1]);
    case FX_SUBST_PUBLISH:
        if(q->bytes || q->arg[2]>UINT32_MAX)return FX_ARGUMENT;
        return fx_overlay_publish(s->store,s->actor,q->arg[1],(uint32_t)q->arg[2],q->arg[3]);
    case FX_SUBST_RESTORE: {
        FxOverlayTarget *t=owned_target(s,q->arg[1]);if(!t)return FX_CAPABILITY;
        if(q->bytes || q->arg[2]>UINT32_MAX)return FX_ARGUMENT;
        return fx_overlay_restore(s->store,s->actor,t->spec.id,(uint32_t)q->arg[2],q->arg[3]);
    }
    case FX_SUBST_STATUS: {
        FxOverlayTarget *t=owned_target(s,q->arg[1]);if(!t)return FX_CAPABILITY;
        if(q->bytes || q->arg[2] || q->arg[3])return FX_ARGUMENT;
        a->value[0]=t->revision;a->value[1]=t->current?s->store->versions[t->current-1u].id:0u;
        a->value[2]=s->store->phase;a->value[3]=s->store->retained;return FX_OK;
    }
    default:return FX_CAPABILITY;
    }
}
