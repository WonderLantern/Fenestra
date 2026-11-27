#include "fenestra_overlay.h"
#include "fenestra_memory.h"

static FxOverlayTarget *target(FxOverlay *s,uint32_t id) {
    for(uint32_t i=0;i<s->target_count;++i)if(s->targets[i].spec.id==id)return &s->targets[i];
    return 0;
}
static FxOverlayVersion *version(FxOverlay *s,uint64_t id) {
    if(!id)return 0;
    for(uint32_t i=0;i<FX_OVERLAY_VERSIONS;++i)
        if(s->versions[i].state && s->versions[i].id==id)return &s->versions[i];
    return 0;
}
static FxResult actor(FxOverlay *s,FxOverlayActor a) {
    if(!s || !a.owner)return FX_ARGUMENT;
    return a.generation==s->generation?FX_OK:FX_STALE;
}
static FxResult publisher(FxOverlay *s,FxOverlayActor a,FxOverlayTarget *t) {
    FxResult r=actor(s,a);if(r!=FX_OK)return r;
    return !t || t->spec.publisher!=a.owner?FX_CAPABILITY:FX_OK;
}
static int overlaps(const void *a,uint32_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<y+m && y<x+n;
}
FxResult fx_overlay_panic(FxOverlay *s,FxResult reason) {
    if(!s)return FX_ARGUMENT;
    if(s->phase!=FX_OVERLAY_FAULT)s->first_fault=reason==FX_OK?FX_STATE:reason;
    s->phase=FX_OVERLAY_FAULT;return s->first_fault;
}
void fx_overlay_init(FxOverlay *s,uint32_t generation,uint64_t budget,FxOverlayMemory memory,FxOverlayPolicy policy) {
    memset(s,0,sizeof(*s));s->generation=generation;s->budget=budget;
    s->memory=memory;s->policy=policy;s->next_id=1u;
}
FxResult fx_overlay_register(FxOverlay *s,const FxOverlayTargetSpec *spec) {
    if(!s || !spec || !s->generation || !spec->id || !spec->publisher ||
       !spec->original || !spec->original_bytes || spec->original_bytes>FX_OVERLAY_OBJECT_LIMIT ||
       !spec->max_bytes || spec->max_bytes>FX_OVERLAY_OBJECT_LIMIT ||
       (spec->flags&~3u) || (uintptr_t)spec->original>UINTPTR_MAX-spec->original_bytes)return FX_ARGUMENT;
    if(overlaps(spec->original,spec->original_bytes,s,sizeof(*s)))return FX_ARGUMENT;
    if(s->phase!=FX_OVERLAY_OFF)return FX_STATE;
    if(s->target_count==FX_OVERLAY_TARGETS)return FX_BUDGET;
    if(target(s,spec->id))return FX_BUSY;
    uint32_t n=0,nonzero=0;
    while(n<sizeof(spec->binding) && spec->binding[n]) {
        if(spec->binding[n]<33 || spec->binding[n]>126)return FX_ARGUMENT;
        ++n;
    }
    if(!n || n==sizeof(spec->binding))return FX_ARGUMENT;
    for(uint32_t i=n;i<sizeof(spec->binding);++i)if(spec->binding[i])return FX_ARGUMENT;
    for(uint32_t i=0;i<s->target_count;++i)
        if(!memcmp(s->targets[i].spec.binding,spec->binding,sizeof(spec->binding)))return FX_BUSY;
    if(spec->flags&FX_OVERLAY_MODEL) {
        for(uint32_t i=0;i<32u;++i)nonzero|=spec->schema_sha256[i];
        if(!nonzero)return FX_ARGUMENT;
    }
    FxOverlayTarget *t=&s->targets[s->target_count++];t->spec=*spec;t->revision=1u;
    fx_sha256_init(&t->original_hash);
    return FX_OK;
}
FxResult fx_overlay_activate(FxOverlay *s) {
    if(!s || !s->generation)return FX_ARGUMENT;
    if(s->phase!=FX_OVERLAY_OFF && s->phase!=FX_OVERLAY_PREPARING)return FX_STATE;
    if(!s->target_count)return FX_EMPTY;
    if(!s->memory.reserve || !s->memory.release || !s->policy.check)return FX_CAPABILITY;
    s->phase=FX_OVERLAY_PREPARING;uint32_t budget=FX_OVERLAY_HASH_STEP;
    for(uint32_t i=0;i<s->target_count;++i) {
        FxOverlayTarget *t=&s->targets[i];if(t->verified)continue;
        uint32_t n=t->spec.original_bytes-t->verify_offset;if(n>budget)n=budget;
        FxResult r=fx_sha256_update(&t->original_hash,t->spec.original+t->verify_offset,n);
        if(r!=FX_OK)return fx_overlay_panic(s,r);
        t->verify_offset+=n;budget-=n;
        if(t->verify_offset==t->spec.original_bytes) {
            uint8_t sha[32];fx_sha256_final(&t->original_hash,sha);
            if(memcmp(sha,t->spec.original_sha256,32u))return fx_overlay_panic(s,FX_INTEGRITY);
            t->verified=1u;
        } else return FX_BUSY;
        if(!budget && i+1u<s->target_count)return FX_BUSY;
    }
    s->phase=FX_OVERLAY_ACTIVE;return FX_OK;
}
static int safe_allocation(FxOverlay *s,const FxOverlayAllocation *a) {
    if(!a->data || !a->capacity || !a->token || (uintptr_t)a->data>UINTPTR_MAX-a->capacity)return 0;
    if(overlaps(a->data,a->capacity,s,sizeof(*s)))return 0;
    for(uint32_t i=0;i<s->target_count;++i)
        if(overlaps(a->data,a->capacity,s->targets[i].spec.original,s->targets[i].spec.original_bytes))return 0;
    for(uint32_t i=0;i<FX_OVERLAY_VERSIONS;++i) {
        const FxOverlayVersion *v=&s->versions[i];
        if(v->state && (v->allocation.token==a->token ||
           overlaps(a->data,a->capacity,v->allocation.data,v->allocation.capacity)))return 0;
    }
    return 1;
}
FxResult fx_overlay_begin(FxOverlay *s,FxOverlayActor a,uint32_t id,uint32_t revision,uint32_t bytes,
                          const uint8_t original_sha[32],const uint8_t replacement_sha[32],uint64_t *out) {
    if(out)*out=0u;
    if(!s || !out || !original_sha || !replacement_sha)return FX_ARGUMENT;
    FxOverlayTarget *t=target(s,id);FxResult r=publisher(s,a,t);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    if(revision!=t->revision)return FX_STALE;
    if(memcmp(original_sha,t->spec.original_sha256,32u))return FX_INTEGRITY;
    if(!bytes || bytes>t->spec.max_bytes || ((t->spec.flags&FX_OVERLAY_SAME_SIZE) && bytes!=t->spec.original_bytes))return FX_BOUNDS;
    if(s->retained>s->budget || bytes>s->budget-s->retained)return FX_BUDGET;
    if(s->next_id==UINT64_MAX)return FX_BUDGET;
    FxOverlayVersion *v=0;
    for(uint32_t i=0;i<FX_OVERLAY_VERSIONS;++i)if(!s->versions[i].state){v=&s->versions[i];break;}
    if(!v)return FX_BUDGET;
    FxOverlayAllocation allocation={0};r=s->memory.reserve(s->memory.context,bytes,&allocation);
    if(r!=FX_OK) {
        if(allocation.data || allocation.token || allocation.capacity) {
            *v=(FxOverlayVersion){.id=s->next_id++,.target=id,.owner=a.owner,.state=FX_OVERLAY_RETIRED,
                .bytes=bytes,.allocation=allocation};
            s->retained+=allocation.capacity;return fx_overlay_panic(s,FX_INTEGRITY);
        }
        return r;
    }
    /* Never dereference an invalid allocator result, including an original-ROM
     * alias. Retain ownership uncertainty and stop admission. */
    int safe=safe_allocation(s,&allocation);
    *v=(FxOverlayVersion){.id=s->next_id++,.target=id,.owner=a.owner,.state=FX_OVERLAY_WRITING,
        .bytes=bytes,.base_revision=revision,.allocation=allocation};
    s->retained+=allocation.capacity;
    if(!safe)return fx_overlay_panic(s,FX_INTEGRITY);
    if(allocation.capacity<bytes || s->retained>s->budget || s->phase!=FX_OVERLAY_ACTIVE) {
        v->state=FX_OVERLAY_RETIRED;
        if(s->phase!=FX_OVERLAY_FAULT)(void)fx_overlay_collect(s);
        return s->phase==FX_OVERLAY_FAULT?FX_STATE:FX_BUDGET;
    }
    memcpy(v->expected_sha256,replacement_sha,32u);fx_sha256_init(&v->hash);*out=v->id;
    return FX_OK;
}
static FxResult own_version(FxOverlay *s,FxOverlayActor a,uint64_t id,FxOverlayVersion **out) {
    FxResult r=actor(s,a);if(r!=FX_OK)return r;
    *out=version(s,id);if(!*out)return FX_STALE;
    return (*out)->owner==a.owner?FX_OK:FX_CAPABILITY;
}
FxResult fx_overlay_feed(FxOverlay *s,FxOverlayActor a,uint64_t id,uint32_t offset,const void *data,uint32_t bytes) {
    FxOverlayVersion *v=0;FxResult r=own_version(s,a,id,&v);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_ACTIVE || v->state!=FX_OVERLAY_WRITING)return FX_STATE;
    if(!data || !bytes || bytes>FX_OVERLAY_CHUNK || bytes>v->bytes-v->written)return FX_BOUNDS;
    if(offset!=v->written)return FX_STALE;
    if((uintptr_t)data>UINTPTR_MAX-bytes || overlaps(data,bytes,v->allocation.data+offset,bytes))return FX_ARGUMENT;
    /* Inputs are kernel-owned request copies; a module pointer never enters. */
    memcpy(v->allocation.data+offset,data,bytes);
    r=fx_sha256_update(&v->hash,v->allocation.data+offset,bytes);
    if(r!=FX_OK)return fx_overlay_panic(s,r);
    v->written+=bytes;return FX_OK;
}
FxResult fx_overlay_seal(FxOverlay *s,FxOverlayActor a,uint64_t id) {
    FxOverlayVersion *v=0;FxResult r=own_version(s,a,id,&v);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_ACTIVE || v->state!=FX_OVERLAY_WRITING)return FX_STATE;
    if(v->written!=v->bytes)return FX_BOUNDS;
    uint8_t digest[32];fx_sha256_final(&v->hash,digest);
    if(memcmp(digest,v->expected_sha256,32u))return FX_INTEGRITY;
    v->state=FX_OVERLAY_READY;return FX_OK;
}
static FxResult check_policy(FxOverlay *s,FxOverlayActor a,FxOverlayTarget *t,
                             uint64_t lease,const uint8_t *data,uint32_t bytes) {
    if(!s->policy.check || !lease)return FX_CAPABILITY;
    FxResult r=s->policy.check(s->policy.context,t->spec.id,a,lease,data,bytes,t->spec.schema_sha256);
    return s->phase!=FX_OVERLAY_ACTIVE?FX_STATE:r;
}
FxResult fx_overlay_publish(FxOverlay *s,FxOverlayActor a,uint64_t id,uint32_t revision,uint64_t lease) {
    FxOverlayVersion *v=0;FxResult r=own_version(s,a,id,&v);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_ACTIVE || v->state!=FX_OVERLAY_READY)return FX_STATE;
    FxOverlayTarget *t=target(s,v->target);if(!t)return fx_overlay_panic(s,FX_INTEGRITY);
    if(t->revision!=revision || v->base_revision!=revision || revision==UINT32_MAX)return FX_STALE;
    r=check_policy(s,a,t,lease,v->allocation.data,v->bytes);if(r!=FX_OK)return r;
    if(t->current)s->versions[t->current-1u].state=FX_OVERLAY_RETIRED;
    t->current=(uint32_t)(v-s->versions)+1u;v->state=FX_OVERLAY_LIVE;++t->revision;
    return fx_overlay_collect(s);
}
FxResult fx_overlay_restore(FxOverlay *s,FxOverlayActor a,uint32_t id,uint32_t revision,uint64_t lease) {
    if(!s)return FX_ARGUMENT;
    FxOverlayTarget *t=target(s,id);FxResult r=publisher(s,a,t);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    if(t->revision!=revision || revision==UINT32_MAX)return FX_STALE;
    if(!t->current)return FX_OK;
    r=check_policy(s,a,t,lease,t->spec.original,t->spec.original_bytes);if(r!=FX_OK)return r;
    s->versions[t->current-1u].state=FX_OVERLAY_RETIRED;t->current=0u;++t->revision;
    return fx_overlay_collect(s);
}
FxResult fx_overlay_abort(FxOverlay *s,FxOverlayActor a,uint64_t id) {
    FxOverlayVersion *v=0;FxResult r=own_version(s,a,id,&v);if(r!=FX_OK)return r;
    if(s->phase==FX_OVERLAY_FAULT)return FX_STATE;
    if(v->state!=FX_OVERLAY_WRITING && v->state!=FX_OVERLAY_READY)return FX_STATE;
    v->state=FX_OVERLAY_RETIRED;return fx_overlay_collect(s);
}
FxResult fx_overlay_open(FxOverlay *s,FxOverlayActor a,uint32_t id,uint32_t write,uint64_t *out,uint32_t *bytes) {
    if(out)*out=0u;
    if(bytes)*bytes=0u;
    if(!s || !out || !bytes)return FX_ARGUMENT;
    if(s->phase==FX_OVERLAY_OFF || s->phase==FX_OVERLAY_PREPARING)return FX_EMPTY;
    FxOverlayTarget *t=target(s,id);if(!t)return FX_EMPTY;
    if(s->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    FxResult r=actor(s,a);if(r!=FX_OK)return r;
    if(write)return FX_CAPABILITY; /* Never redirect a write onto a replacement. */
    if(!t->current)return FX_EMPTY; /* Positive absence only: use native original. */
    if(s->next_id==UINT64_MAX)return FX_BUDGET;
    FxOverlayVersion *v=&s->versions[t->current-1u];
    if(v->state!=FX_OVERLAY_LIVE)return fx_overlay_panic(s,FX_INTEGRITY);
    for(uint32_t i=0;i<FX_OVERLAY_HANDLES;++i)if(!s->handles[i].id) {
        s->handles[i]=(FxOverlayHandle){s->next_id++,a.owner,id,t->current,0u};
        ++v->refs;*out=s->handles[i].id;*bytes=v->bytes;return FX_OK;
    }
    return FX_BUDGET;
}
static FxResult handle(FxOverlay *s,FxOverlayActor a,uint64_t id,FxOverlayHandle **out) {
    FxResult r=actor(s,a);if(r!=FX_OK)return r;
    for(uint32_t i=0;i<FX_OVERLAY_HANDLES;++i)if(id && s->handles[i].id==id) {
        if(s->handles[i].owner!=a.owner)return FX_CAPABILITY;
        *out=&s->handles[i];return FX_OK;
    }
    return FX_STALE;
}
FxResult fx_overlay_view(FxOverlay *s,FxOverlayActor a,uint64_t id,const uint8_t **data,uint32_t *bytes) {
    if(!data || !bytes)return FX_ARGUMENT;
    *data=0;*bytes=0u;FxOverlayHandle *h=0;FxResult r=handle(s,a,id,&h);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    const FxOverlayVersion *v=&s->versions[h->version-1u];
    *data=v->allocation.data;*bytes=v->bytes;return FX_OK;
}
FxResult fx_overlay_read(FxOverlay *s,FxOverlayActor a,uint64_t id,void *data,uint32_t n,uint32_t *actual) {
    if(!actual || (!data && n))return FX_ARGUMENT;
    *actual=0u;FxOverlayHandle *h=0;FxResult r=handle(s,a,id,&h);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    const FxOverlayVersion *v=&s->versions[h->version-1u];
    if(n>v->bytes-h->position)n=v->bytes-h->position;
    if(n) {
        if((uintptr_t)data>UINTPTR_MAX-n || overlaps(data,n,s,sizeof(*s)))return FX_ARGUMENT;
        for(uint32_t i=0;i<s->target_count;++i)
            if(overlaps(data,n,s->targets[i].spec.original,s->targets[i].spec.original_bytes))return FX_ARGUMENT;
        for(uint32_t i=0;i<FX_OVERLAY_VERSIONS;++i)
            if(s->versions[i].state && overlaps(data,n,s->versions[i].allocation.data,s->versions[i].allocation.capacity))return FX_ARGUMENT;
    }
    if(n)memcpy(data,v->allocation.data+h->position,n);
    h->position+=n;*actual=n;return FX_OK;
}
FxResult fx_overlay_seek(FxOverlay *s,FxOverlayActor a,uint64_t id,int64_t offset,uint32_t whence,uint32_t *position) {
    if(!position)return FX_ARGUMENT;
    FxOverlayHandle *h=0;FxResult r=handle(s,a,id,&h);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    uint32_t bytes=s->versions[h->version-1u].bytes;
    if(whence>2u)return FX_ARGUMENT;
    int64_t base=whence==0u?0:(whence==1u?(int64_t)h->position:(int64_t)bytes);
    if(offset< -base || offset>(int64_t)bytes-base)return FX_BOUNDS;
    h->position=(uint32_t)(base+offset);*position=h->position;return FX_OK;
}
FxResult fx_overlay_close(FxOverlay *s,FxOverlayActor a,uint64_t id) {
    FxOverlayHandle *h=0;FxResult r=handle(s,a,id,&h);if(r!=FX_OK)return r;
    FxOverlayVersion *v=&s->versions[h->version-1u];
    if(!v->refs)return fx_overlay_panic(s,FX_INTEGRITY);
    --v->refs;memset(h,0,sizeof(*h));
    return s->phase==FX_OVERLAY_FAULT?FX_OK:fx_overlay_collect(s);
}
FxResult fx_overlay_collect(FxOverlay *s) {
    if(!s)return FX_ARGUMENT;
    if(s->phase==FX_OVERLAY_FAULT)return FX_STATE;
    for(uint32_t i=0;i<FX_OVERLAY_VERSIONS;++i) {
        FxOverlayVersion *v=&s->versions[i];
        if(v->state!=FX_OVERLAY_RETIRED || v->refs)continue;
        FxResult r=s->memory.release(s->memory.context,v->allocation.token);
        if(r!=FX_OK)return fx_overlay_panic(s,r);
        s->retained-=v->allocation.capacity;memset(v,0,sizeof(*v));
        if(s->phase==FX_OVERLAY_FAULT)return FX_STATE;
    }
    return FX_OK;
}
FxResult fx_overlay_stop(FxOverlay *s) {
    if(!s)return FX_ARGUMENT;
    if(s->phase==FX_OVERLAY_FAULT)return FX_STATE;
    if(s->phase==FX_OVERLAY_OFF)return FX_OK;
    s->phase=FX_OVERLAY_QUIESCING;
    for(uint32_t i=0;i<FX_OVERLAY_VERSIONS;++i)
        if(s->versions[i].state==FX_OVERLAY_WRITING || s->versions[i].state==FX_OVERLAY_READY)
            s->versions[i].state=FX_OVERLAY_RETIRED;
    FxResult r=fx_overlay_collect(s);if(r!=FX_OK)return r;
    return s->retained?FX_BUSY:FX_OK;
}
FxResult fx_overlay_drain(FxOverlay *s,FxOverlayActor a,uint32_t id,uint64_t lease) {
    if(!s)return FX_ARGUMENT;
    FxOverlayTarget *t=target(s,id);FxResult r=publisher(s,a,t);if(r!=FX_OK)return r;
    if(s->phase!=FX_OVERLAY_QUIESCING)return FX_STATE;
    if(!t->current)return FX_OK;
    if(!s->policy.check || !lease)return FX_CAPABILITY;
    r=s->policy.check(s->policy.context,id,a,lease,t->spec.original,t->spec.original_bytes,t->spec.schema_sha256);
    if(s->phase!=FX_OVERLAY_QUIESCING)return FX_STATE;
    if(r!=FX_OK)return r;
    s->versions[t->current-1u].state=FX_OVERLAY_RETIRED;t->current=0u;
    return fx_overlay_collect(s);
}
