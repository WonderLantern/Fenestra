#include "fenestra_capacity.h"
#include "fenestra_file_observer.h"
#include "fenestra_memory.h"
struct FxCapacityWatch {
    FxCapacityWatch *next,*owner_next;FxCapacityDomain *domain;FxPlatformOwner *owner;
    _Atomic uint64_t armed;
};
static int same(const char *a,const char *b) {
    for(uint32_t i=0u;i<64u;++i){if(a[i]!=b[i])return 0;if(!a[i])return 1;}return 0;
}
static int name_valid(const char *name) {
    if(!name || !name[0])return 0;
    for(uint32_t i=0u;i<64u;++i) {
        char c=name[i];if(!c)return i!=0u;
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='.' || c=='_' || c=='-'))return 0;
    }
    return 0;
}
static void maximum(_Atomic uint64_t *p,uint64_t value) {
    uint64_t old=atomic_load(p);while(old<value && !atomic_compare_exchange_weak(p,&old,value)){}
}
static void invalid(FxCapacityRegistry *r){atomic_fetch_or(&r->flags,FX_CAPACITY_INVALID);}
static int add(FxCapacityRegistry *r,_Atomic uint64_t *p,uint64_t n) {
    uint64_t old=atomic_load(p);
    for(;;){if(n>UINT64_MAX-old){invalid(r);return 0;}if(atomic_compare_exchange_weak(p,&old,old+n))return 1;}
}
static int subtract(FxCapacityRegistry *r,_Atomic uint64_t *p,uint64_t n) {
    uint64_t old=atomic_load(p);
    for(;;){if(n>old){invalid(r);return 0;}if(atomic_compare_exchange_weak(p,&old,old-n))return 1;}
}
FxResult fx_capacity_init(FxCapacityRegistry *r,FxIndexMemory memory,FxAllocationObserver observer) {
    if(!r || !memory.allocate || !memory.release)return FX_ARGUMENT;
    if(r->memory.allocate)return FX_BUSY;
    r->memory=memory;r->metadata=observer;return FX_OK;
}
FxResult fx_capacity_register(FxCapacityRegistry *r,FxCapacityDomain *d,const char *name,uint64_t limit,uint32_t exhausted) {
    if(!r || !r->memory.allocate || !d || d->registry || atomic_load(&r->ready) || !name_valid(name))return FX_ARGUMENT;
    for(FxCapacityDomain *p=r->domains;p;p=p->next)if(same(p->name,name))return FX_BUSY;
    if(r->domain_count==UINT32_MAX)return FX_BUDGET;
    uint32_t length=0u;while(name[length])++length;
    d->registry=r;memcpy(d->name,name,length+1u);d->limit=limit;d->exhausted=exhausted;
    d->flags=FX_CAPACITY_PARTIAL|FX_CAPACITY_APPROXIMATE|(limit?FX_CAPACITY_KNOWN_LIMIT:0u);
    d->id=++r->domain_count;atomic_store(&d->epoch,1u);d->next=r->domains;r->domains=d;return FX_OK;
}
void fx_capacity_publish(FxCapacityRegistry *r) {
    if(r && r->memory.allocate)atomic_store_explicit(&r->ready,1u,memory_order_release);
}
FxResult fx_capacity_seed(FxCapacityDomain *d,uint64_t used,uint64_t retained) {
    if(!d || !d->registry || atomic_load_explicit(&d->registry->ready,memory_order_acquire) || retained>used)return FX_ARGUMENT;
    atomic_store(&d->used,used);atomic_store(&d->peak,used);atomic_store(&d->retained,retained);return FX_OK;
}
FxCapacityDomain *fx_capacity_find(FxCapacityRegistry *r,const char *name) {
    if(!r || !name || !atomic_load_explicit(&r->ready,memory_order_acquire))return 0;
    for(FxCapacityDomain *p=r->domains;p;p=p->next)if(same(p->name,name))return p;
    return 0;
}
FxCapacityClaim fx_capacity_acquire(FxCapacityDomain *d,uint64_t units) {
    FxCapacityClaim claim={0};if(!d || !units || !atomic_load_explicit(&d->registry->ready,memory_order_acquire))return claim;
    /* Native allocation ALREADY succeeded. This never compares limit. */
    if(add(d->registry,&d->used,units)){claim=(FxCapacityClaim){d,units,0u};maximum(&d->peak,atomic_load(&d->used));}
    (void)add(d->registry,&d->acquired,1u);return claim;
}
void fx_capacity_failed(FxCapacityDomain *d,uint32_t error) {
    if(!d || !atomic_load_explicit(&d->registry->ready,memory_order_acquire))return;
    atomic_store(&d->last_error,error);(void)add(d->registry,&d->failed,1u);
    if(d->exhausted && error==d->exhausted)(void)add(d->registry,&d->pressure,1u);
}
FxCapacityClaim fx_capacity_file_open(FxCapacityDomain *d,int32_t result) {
    if(result<0){fx_capacity_failed(d,(uint32_t)result);return (FxCapacityClaim){0};}
    return fx_capacity_acquire(d,1u);
}
static FxCapacityClaim file_opened(void *context,int32_t result) {
    return fx_capacity_file_open(fx_capacity_find(context,"camera.card.streams.a"),result);
}
FxFileObserver fx_capacity_file_observer(FxCapacityRegistry *r) {
    return (FxFileObserver){r,file_opened,fx_capacity_release,fx_capacity_retain};
}
void fx_capacity_retain(FxCapacityClaim *c) {
    if(c && c->domain && !c->retained && add(c->domain->registry,&c->domain->retained,c->units))c->retained=1u;
}
static void notify(FxCapacityDomain *d) {
    if(!add(d->registry,&d->epoch,1u))return;
    if(!atomic_load(&d->active_watches))return; /* Dormant owners cost O(1). */
    for(FxCapacityWatch *w=atomic_load(&d->watches);w;w=w->next) {
        uint64_t armed=atomic_load(&w->armed);
        /* SC arm/recheck and release/publication exclude both-old reads.
         * Do not consume a subscription to a later release generation. */
        while(armed && armed!=atomic_load(&d->epoch))
            if(atomic_compare_exchange_weak(&w->armed,&armed,0u)){
                atomic_fetch_sub(&d->active_watches,1u);fx_platform_wake(w->owner);break;
            }
    }
}
void fx_capacity_release(FxCapacityClaim *c,uint32_t result) {
    if(!c || !c->domain)return;
    FxCapacityDomain *d=c->domain;
    if(result){atomic_store(&d->last_error,result);fx_capacity_retain(c);return;}
    if(!subtract(d->registry,&d->used,c->units))return;
    if(c->retained)(void)subtract(d->registry,&d->retained,c->units);
    (void)add(d->registry,&d->released,1u);*c=(FxCapacityClaim){0};notify(d);
}
void fx_capacity_info(const FxCapacityDomain *d,FxCapacityInfo *out) {
    if(!d || !out)return;
    uint64_t used=atomic_load(&d->used),peak=atomic_load(&d->peak);if(peak<used)peak=used;
    *out=(FxCapacityInfo){.abi=1u,.bytes=sizeof(*out),.flags=d->flags|atomic_load(&d->registry->flags),
        .last_error=atomic_load(&d->last_error),.id=d->id,.limit=d->limit,.used=used,.peak=peak,
        .retained=atomic_load(&d->retained),.acquired=atomic_load(&d->acquired),.failed=atomic_load(&d->failed),
        .pressure=atomic_load(&d->pressure),.released=atomic_load(&d->released),.epoch=atomic_load(&d->epoch)};
    memcpy(out->name,d->name,sizeof(out->name));
}
static FxCapacityWatch *watch(FxPlatformOwner *o,FxCapacityDomain *d) {
    for(FxCapacityWatch *w=o->capacity_watches;w;w=w->owner_next)if(w->domain==d)return w;
    FxCapacityRegistry *r=d->registry;uint32_t old=atomic_load(&r->metadata_used),bytes=(uint32_t)sizeof(FxCapacityWatch);
    for(;;) {
        if(old>FX_CAPACITY_METADATA_BUDGET-bytes){atomic_fetch_or(&r->flags,FX_CAPACITY_METADATA_FAILED);return 0;}
        if(atomic_compare_exchange_weak(&r->metadata_used,&old,old+bytes))break;
    }
    uint32_t peak=atomic_load(&r->metadata_peak);
    while(peak<old+bytes && !atomic_compare_exchange_weak(&r->metadata_peak,&peak,old+bytes)){}
    FxCapacityWatch *w=r->memory.allocate(r->memory.context,bytes);
    if(!w || ((uintptr_t)w&7u)) {
        if(w)r->memory.release(r->memory.context,w);
        atomic_fetch_sub(&r->metadata_used,bytes);atomic_fetch_or(&r->flags,FX_CAPACITY_METADATA_FAILED);return 0;
    }
    fx_allocation_observe(r->metadata,bytes,FX_ALLOC_REQUESTED);memset(w,0,bytes);w->owner=o;w->domain=d;
    w->owner_next=o->capacity_watches;o->capacity_watches=w;
    w->next=atomic_load(&d->watches);
    while(!atomic_compare_exchange_weak(&d->watches,&w->next,w)){}
    fx_allocation_observe(r->metadata,bytes,FX_ALLOC_PUBLISHED);return w;
}
static void cancel(FxPlatformOwner *o) {
    for(FxCapacityWatch *w=o->capacity_watches;w;w=w->owner_next)
        if(atomic_exchange(&w->armed,0u))atomic_fetch_sub(&w->domain->active_watches,1u);
}
FxResult fx_capacity_resource(void *context,uint32_t op,const FxNamedBinding *binding,FxPlatformOwner *o,
    uint64_t token,uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;if(!o || !cookie)return FX_ARGUMENT;
    FxCapacityRegistry *r=o->capacity;
    if(!r || !atomic_load_explicit(&r->ready,memory_order_acquire))return FX_CAPABILITY;
    if(op==FX_RESOURCE_ATTACH){*cookie=(uintptr_t)r;return FX_OK;}
    if(*cookie!=(uintptr_t)r)return FX_STALE;
    if(op==FX_RESOURCE_PANIC){cancel(o);return FX_OK;}
    if(op==FX_RESOURCE_DETACH) {
        for(uint32_t i=0u;i<o->bindings.lease_capacity;++i) {
            const FxBindingLease *l=fx_binding_store_lease_at(&o->bindings,i);
            if(l && l->token!=token && l->attached==1u && fx_platform_binding(o,l->index)==binding){*cookie=0u;return FX_OK;}
        }
        cancel(o);*cookie=0u;return FX_OK;
    }
    if(op!=FX_RESOURCE_CALL || !q || !a)return FX_ARGUMENT;
    if(q->arg[2]==FX_CAPACITY_ENUM) {
        if(q->bytes)return FX_ARGUMENT;
        FxCapacityDomain *next=0;
        for(FxCapacityDomain *d=r->domains;d;d=d->next)if(d->id>q->arg[3] && (!next || d->id<next->id))next=d;
        if(!next)return FX_EMPTY;
        FxCapacityInfo info;fx_capacity_info(next,&info);memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);a->value[0]=next->id;return FX_OK;
    }
    if(q->arg[3] || q->bytes!=sizeof(FxCapacityRequest))return FX_ARGUMENT;
    FxCapacityRequest request;memcpy(&request,q->data,sizeof(request));
    if(!fx_service_name(request.name,sizeof(request.name),1))return FX_ARGUMENT;
    FxCapacityDomain *d=fx_capacity_find(r,request.name);if(!d)return FX_EMPTY;
    if(q->arg[2]==FX_CAPACITY_QUERY) {
        if(request.observed)return FX_ARGUMENT;
    } else if(q->arg[2]==FX_CAPACITY_WATCH) {
        if(!request.observed || request.observed>atomic_load(&d->epoch))return FX_ARGUMENT;
        if(!atomic_load_explicit(&o->wake_callback,memory_order_acquire))return FX_CAPABILITY;
        FxCapacityWatch *w=watch(o,d);if(!w)return FX_BUDGET;
        /* Reserve the count before publishing armed. Replacement drops the
         * prior count after exchange; concurrent consumers always own one.
         * Thus a producer's zero fast path cannot miss a published watch. */
        atomic_fetch_add(&d->active_watches,1u);
        if(atomic_exchange(&w->armed,request.observed))atomic_fetch_sub(&d->active_watches,1u);
        if(atomic_load(&d->epoch)!=request.observed) {
            if(atomic_exchange(&w->armed,0u)){atomic_fetch_sub(&d->active_watches,1u);fx_platform_wake(o);}
        }
        a->value[1]=atomic_load(&w->armed)?1u:0u;
    } else if(q->arg[2]==FX_CAPACITY_CANCEL) {
        if(request.observed)return FX_ARGUMENT;
        for(FxCapacityWatch *w=o->capacity_watches;w;w=w->owner_next)if(w->domain==d)
            if(atomic_exchange(&w->armed,0u))atomic_fetch_sub(&d->active_watches,1u);
    } else return FX_ARGUMENT;
    FxCapacityInfo info;fx_capacity_info(d,&info);memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);a->value[0]=info.epoch;return FX_OK;
}
FxResult fx_capacity_destroy(FxCapacityRegistry *r) {
    if(!r || !r->memory.allocate)return FX_ARGUMENT;
    for(FxCapacityDomain *d=r->domains;d;d=d->next)if(atomic_load(&d->used))return FX_BUSY;
    for(FxCapacityDomain *d=r->domains;d;) {
        FxCapacityDomain *next=d->next;
        for(FxCapacityWatch *w=atomic_load(&d->watches);w;) {
            FxCapacityWatch *after=w->next;w->owner->capacity_watches=0;
            r->memory.release(r->memory.context,w);fx_allocation_observe(r->metadata,(uint32_t)sizeof(*w),FX_ALLOC_FREE_PUBLISHED);w=after;
        }
        memset(d,0,sizeof(*d));d=next;
    }
    memset(r,0,sizeof(*r));return FX_OK;
}
