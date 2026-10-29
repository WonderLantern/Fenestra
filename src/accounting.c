#include "fenestra_accounting.h"
#include "fenestra_memory.h"
#include "fenestra_accounting_service.h"
#include "fenestra_shared_accounts.h"
#include "fenestra_module_bus.h"
#include "fenestra_hooks.h"
#include "fenestra_native_font.h"
static void peak(_Atomic uint64_t *p,uint64_t value) {
    uint64_t old=atomic_load_explicit(p,memory_order_relaxed);
    while(old<value && !atomic_compare_exchange_weak_explicit(p,&old,value,memory_order_relaxed,memory_order_relaxed)){}
}
static int add(_Atomic uint64_t *p,uint64_t n,uint64_t *value) {
    uint64_t old=atomic_load_explicit(p,memory_order_seq_cst);
    do {if(n>UINT64_MAX-old)return 0;}while(!atomic_compare_exchange_weak_explicit(p,&old,old+n,memory_order_seq_cst,memory_order_seq_cst));
    if(value)*value=old+n;
    return 1;
}
static int subtract(_Atomic uint64_t *p,uint64_t n) {
    uint64_t old=atomic_load_explicit(p,memory_order_seq_cst);
    do {if(n>old)return 0;}while(!atomic_compare_exchange_weak_explicit(p,&old,old-n,memory_order_seq_cst,memory_order_seq_cst));
    return 1;
}
static FxResult invalid(FxAccountState *s) {atomic_fetch_or(&s->flags,FX_ACCOUNT_INVALID);return FX_STATE;}
static void owned_add(FxAccountState *s,uint64_t n) {
    uint64_t total;if(add(&s->used,n,&total))peak(&s->peak,total);else (void)invalid(s);
}
static void owned_release(FxAccountState *s,uint64_t n) {if(!subtract(&s->used,n))(void)invalid(s);}
FxResult fx_account_init(FxAccountState *s,uint32_t owner,uint32_t generation,FxIndexMemory memory) {
    if(!s || !generation || (!!memory.allocate!=!!memory.release))return FX_ARGUMENT;
    if(s->generation)return FX_BUSY;
    memset(s,0,sizeof(*s));s->memory=memory;s->owner=owner;s->generation=generation;s->metadata_limit=FX_ACCOUNT_METADATA_BUDGET;return FX_OK;
}
static int same(const char *a,const char *b) {
    for(uint32_t i=0;i<64u;++i){if(a[i]!=b[i])return 0;if(!a[i])return 1;}return 0;
}
static FxAccountRow *find(FxAccountRow *head,const char *name) {
    for(FxAccountRow *p=head;p;p=p->next)if(same(p->name,name))return p;
    return 0;
}
static int reserve(FxAccountState *s) {
    uint32_t old=atomic_load_explicit(&s->metadata_reserved,memory_order_relaxed),bytes=(uint32_t)sizeof(FxAccountRow);
    do {if(old>s->metadata_limit || bytes>s->metadata_limit-old)return 0;}
    while(!atomic_compare_exchange_weak_explicit(&s->metadata_reserved,&old,old+bytes,memory_order_acq_rel,memory_order_relaxed));return 1;
}
static void release_row(FxAccountState *s,FxAccountRow *p) {
    s->memory.release(s->memory.context,p);owned_release(s,sizeof(*p));
    atomic_fetch_sub(&s->metadata_used,(uint32_t)sizeof(*p));atomic_fetch_sub(&s->metadata_reserved,(uint32_t)sizeof(*p));
}
FxResult fx_account_get(FxAccountState *s,const char *name,uint32_t flags,uint64_t budget,FxAccountRow **out) {
    if(!s || !s->generation || !name || !out || *out || !name[0] || (flags&~15u))return FX_ARGUMENT;
    uint32_t kind=flags&7u;if(kind!=FX_ACCOUNT_MANAGED && kind!=FX_ACCOUNT_VIEW && kind!=FX_ACCOUNT_EXTERNAL)return FX_ARGUMENT;
    uint32_t n=0u;while(n<64u && name[n])++n;if(n==64u)return FX_ARGUMENT;
    FxAccountRow *head=atomic_load_explicit(&s->rows,memory_order_acquire),*p=find(head,name);
    if(p){if((p->flags&15u)!=flags || p->budget!=budget)return FX_STATE;*out=p;return FX_OK;}
    if(!s->memory.allocate)return FX_CAPABILITY;
    if(!reserve(s)){atomic_fetch_or(&s->flags,FX_ACCOUNT_METADATA_FAILED);return FX_BUDGET;}
    p=s->memory.allocate(s->memory.context,(uint32_t)sizeof(*p));
    if(!p){atomic_fetch_sub(&s->metadata_reserved,(uint32_t)sizeof(*p));atomic_fetch_or(&s->flags,FX_ACCOUNT_METADATA_FAILED);return FX_BUDGET;}
    atomic_fetch_add(&s->metadata_used,(uint32_t)sizeof(*p));owned_add(s,sizeof(*p));
    memset(p,0,sizeof(*p));p->state=s;p->flags=flags;p->budget=budget;memcpy(p->name,name,n+1u);
    uint64_t id=atomic_load_explicit(&s->next_id,memory_order_relaxed);
    do {if(id==UINT64_MAX){release_row(s,p);return FX_BUDGET;}}
    while(!atomic_compare_exchange_weak_explicit(&s->next_id,&id,id+1u,memory_order_relaxed,memory_order_relaxed));p->id=id+1u;
    for(;;) {
        FxAccountRow *existing=find(head,name);
        if(existing) {
            release_row(s,p);if((existing->flags&15u)!=flags || existing->budget!=budget)return FX_STATE;*out=existing;return FX_OK;
        }
        p->next=head;
        if(atomic_compare_exchange_weak_explicit(&s->rows,&head,p,memory_order_release,memory_order_acquire))break;
    }
    atomic_fetch_add(&s->row_count,1u);uint64_t ignored;
    if(!add(&s->retained,sizeof(*p),&ignored))(void)invalid(s);
    *out=p;return FX_OK;
}
FxResult fx_account_register_static(FxAccountState *s,FxAccountRow *p,const char *name,uint32_t flags,uint64_t budget) {
    if(!s || !s->generation || !p || p->state || !name || !name[0] || (flags&~15u))return FX_ARGUMENT;
    uint32_t kind=flags&7u;if(kind!=FX_ACCOUNT_MANAGED && kind!=FX_ACCOUNT_VIEW && kind!=FX_ACCOUNT_EXTERNAL)return FX_ARGUMENT;
    uint32_t n=0u;while(n<64u && name[n])++n;if(n==64u)return FX_ARGUMENT;
    FxAccountRow *head=atomic_load_explicit(&s->rows,memory_order_acquire);
    if(find(head,name))return FX_STATE;
    uint64_t id=atomic_load(&s->next_id);
    do {if(id==UINT64_MAX)return FX_BUDGET;}while(!atomic_compare_exchange_weak(&s->next_id,&id,id+1u));
    memset(p,0,sizeof(*p));p->state=s;p->id=id+1u;p->flags=flags|FX_ACCOUNT_STATIC;p->budget=budget;memcpy(p->name,name,n+1u);
    do {
        if(find(head,name)){memset(p,0,sizeof(*p));return FX_STATE;}
        p->next=head;
    }while(!atomic_compare_exchange_weak_explicit(&s->rows,&head,p,memory_order_release,memory_order_acquire));
    atomic_fetch_add(&s->row_count,1u);return FX_OK;
}
FxResult fx_account_acquire(FxAccountRow *p,uint64_t bytes,uint64_t units,uint32_t retained) {
    if(!p || !p->state || retained>1u || ((p->flags&FX_ACCOUNT_EXTERNAL) && bytes))return FX_ARGUMENT;
    uint64_t value;FxAccountState *s=p->state;
    if(!add(&p->used,bytes,&value))return invalid(s);
    peak(&p->peak,value);
    if(!add(&p->units,units,&value))return invalid(s);
    peak(&p->units_peak,value);
    if(p->flags&FX_ACCOUNT_MANAGED)owned_add(s,bytes);
    return retained?fx_account_retain(p,bytes,units):FX_OK;
}
FxResult fx_account_retain(FxAccountRow *p,uint64_t bytes,uint64_t units) {
    if(!p || !p->state || ((p->flags&FX_ACCOUNT_EXTERNAL) && bytes))return FX_ARGUMENT;
    FxAccountState *s=p->state;uint64_t n;
    if(!add(&p->retained,bytes,&n) || !add(&p->retained_units,units,&n))return invalid(s);
    if((p->flags&FX_ACCOUNT_MANAGED) && !add(&s->retained,bytes,&n))return invalid(s);
    return FX_OK;
}
FxResult fx_account_unretain(FxAccountRow *p,uint64_t bytes,uint64_t units) {
    if(!p || !p->state || ((p->flags&FX_ACCOUNT_EXTERNAL) && bytes))return FX_ARGUMENT;
    FxAccountState *s=p->state;
    if(!subtract(&p->retained,bytes) || !subtract(&p->retained_units,units))return invalid(s);
    if((p->flags&FX_ACCOUNT_MANAGED) && !subtract(&s->retained,bytes))return invalid(s);
    return FX_OK;
}
FxResult fx_account_release(FxAccountRow *p,uint64_t bytes,uint64_t units,uint32_t retained) {
    if(!p || !p->state || retained>1u || ((p->flags&FX_ACCOUNT_EXTERNAL) && bytes))return FX_ARGUMENT;
    FxAccountState *s=p->state;
    if(retained && fx_account_unretain(p,bytes,units)!=FX_OK)return FX_STATE;
    if(!subtract(&p->used,bytes) || !subtract(&p->units,units))return invalid(s);
    if(p->flags&FX_ACCOUNT_MANAGED)owned_release(s,bytes);
    return FX_OK;
}
static void allocation_event(void *context,uint32_t bytes,uint32_t op) {
    FxAccountRow *row=context;
    if(op==FX_ALLOC_REQUESTED)(void)fx_account_acquire(row,bytes,1u,0u);
    else if(op==FX_ALLOC_PUBLISHED)(void)fx_account_retain(row,bytes,1u);
    else if(op==FX_ALLOC_FREE_PENDING || op==FX_ALLOC_FREE_PUBLISHED)
        (void)fx_account_release(row,bytes,1u,(uint32_t)(op==FX_ALLOC_FREE_PUBLISHED));
    else if(op==FX_ALLOC_UNTRACKED)atomic_fetch_or(&row->state->flags,FX_ACCOUNT_PARTIAL|FX_ACCOUNT_METADATA_FAILED);
}
FxAllocationObserver fx_account_observer(FxAccountRow *row) {
    return row?(FxAllocationObserver){row,allocation_event}:(FxAllocationObserver){0};
}
static FxAllocationObserver owner_observer(FxPlatformOwner *,const char *);
void fx_account_owner_bind(FxPlatformOwner *owner) {
    if(!owner || !owner->accounting)return;
    owner->account_observer=owner_observer;
    if(owner->bindings.observer.event)return;
    FxAccountRow *row=0;
    if(fx_account_get(owner->accounting,"binding.metadata",FX_ACCOUNT_MANAGED,FX_BINDING_METADATA_BUDGET,&row)==FX_OK)
        owner->bindings.observer=fx_account_observer(row);
}
static FxAllocationObserver owner_observer(FxPlatformOwner *owner,const char *name) {
    FxAccountRow *row=0;
    if(owner && owner->accounting)(void)fx_account_get(owner->accounting,name,FX_ACCOUNT_MANAGED,0u,&row);
    return fx_account_observer(row);
}
static void copied_queue_event(void *context,uint32_t bytes,uint32_t operation) {
    /* Copied queues expose no native pointer. Publication alone does not pin
     * them past endpoint/owner removal; their normal stop frees them. */
    if(operation==FX_ALLOC_REQUESTED)(void)fx_account_acquire(context,bytes,1u,0u);
    else if(operation==FX_ALLOC_FREE_PENDING || operation==FX_ALLOC_FREE_PUBLISHED)
        (void)fx_account_release(context,bytes,1u,0u);
}
FxAllocationObserver fx_account_bus_queues(FxPlatformOwner *o){
    FxAllocationObserver observer=owner_observer(o,"bus.queues");if(observer.event)observer.event=copied_queue_event;return observer;
}
FxAllocationObserver fx_account_task_storage(FxPlatformOwner *o){return owner_observer(o,"task.sessions");}
FxAllocationObserver fx_account_hook_storage(FxPlatformOwner *o){return owner_observer(o,"hook.storage");}
static void retained_ui(void *context,_Atomic uint32_t *tag,uint32_t bytes) {
    FxSharedAccounts *s=context;
    if(atomic_load_explicit(&s->ready,memory_order_seq_cst) && !atomic_exchange(tag,1u))
        (void)fx_account_acquire(&s->ui,bytes,1u,1u);
}
static void retained_text(void *context,_Atomic uint32_t *tag,uint32_t bytes) {
    FxSharedAccounts *s=context;
    if(atomic_load_explicit(&s->ready,memory_order_seq_cst) && !atomic_exchange(tag,1u))
        (void)fx_account_acquire(&s->text,bytes,1u,1u);
}
FxRetainedAccount fx_shared_ui_account(FxSharedAccounts *s){return (FxRetainedAccount){s,retained_ui};}
FxRetainedAccount fx_shared_text_account(FxSharedAccounts *s){return (FxRetainedAccount){s,retained_text};}
static void retained_loader(void *context,_Atomic uint32_t *tag,uint32_t bytes) {
    FxSharedAccounts *s=context;
    if(atomic_load_explicit(&s->ready,memory_order_seq_cst) && !atomic_exchange(tag,1u))
        (void)fx_account_acquire(&s->loader,bytes,1u,1u);
}
FxRetainedAccount fx_shared_loader_account(FxSharedAccounts *s){return (FxRetainedAccount){s,retained_loader};}
FxResult fx_shared_accounts_init(FxSharedAccounts *s,FxIndexMemory memory,uint32_t controller_bytes,uint32_t domain_bytes) {
    if(!s || !controller_bytes || !domain_bytes)return FX_ARGUMENT;
    FxResult r=fx_account_init(&s->state,0u,1u,memory);if(r!=FX_OK)return r;
    const uint32_t scope=FX_ACCOUNT_MANAGED|FX_ACCOUNT_SHARED;
    r=fx_account_register_static(&s->state,&s->controller,"core.controller",scope,0u);
    if(r==FX_OK)r=fx_account_register_static(&s->state,&s->bus,"bus.metadata",scope,FX_BUS_BUDGET);
    if(r==FX_OK)r=fx_account_register_static(&s->state,&s->pages,"hook.pages",scope,FX_HOOK_BUDGET);
    if(r==FX_OK)r=fx_account_register_static(&s->state,&s->domain,"task.domain",FX_ACCOUNT_VIEW|FX_ACCOUNT_SHARED,0u);
    if(r==FX_OK)r=fx_account_register_static(&s->state,&s->ui,"ui.shared",scope,0u);
    if(r==FX_OK)r=fx_account_register_static(&s->state,&s->text,"text.banks",scope,FX_NATIVE_TEXT_BUDGET);
    if(r==FX_OK)r=fx_account_register_static(&s->state,&s->loader,"loader.storage",scope,0u);
    if(r==FX_OK)r=fx_account_register_static(&s->state,&s->indexes,"module.indexes",scope,0u);
    if(r==FX_OK)r=fx_account_register_static(&s->state,&s->inspect,"module.inspect",scope,0u);
    if(r==FX_OK){(void)fx_account_acquire(&s->controller,controller_bytes,1u,1u);(void)fx_account_acquire(&s->domain,domain_bytes,1u,1u);}
    /* Readiness and retained-root publication/seeding share the SC order.
     * This excludes both the publisher and activator observing old roots. */
    atomic_fetch_or(&s->state.flags,FX_ACCOUNT_PARTIAL);if(r==FX_OK)atomic_store_explicit(&s->ready,1u,memory_order_seq_cst);return r;
}
void fx_account_info(const FxAccountState *s,FxAccountInfo *out) {
    if(!s || !out)return;
    uint64_t used=atomic_load(&s->used),maximum=atomic_load(&s->peak);if(maximum<used)maximum=used;
    *out=(FxAccountInfo){1u,sizeof(*out),s->owner,s->generation,atomic_load(&s->flags)|FX_ACCOUNT_APPROXIMATE,
        atomic_load(&s->row_count),atomic_load(&s->metadata_used),atomic_load(&s->metadata_reserved),
        used,maximum,atomic_load(&s->retained),s->metadata_limit};
}
FxResult fx_account_next(const FxAccountState *s,uint64_t after,FxAccountRowInfo *out) {
    if(!s || !out)return FX_ARGUMENT;
    const FxAccountRow *next=0;
    for(FxAccountRow *p=atomic_load_explicit(&s->rows,memory_order_acquire);p;p=p->next)
        if(p->id>after && (!next || p->id<next->id))next=p;
    if(!next)return FX_EMPTY;
    uint64_t used=atomic_load(&next->used),maximum=atomic_load(&next->peak);if(maximum<used)maximum=used;
    *out=(FxAccountRowInfo){.abi=1u,.bytes=sizeof(*out),.flags=next->flags,.id=next->id,.budget=next->budget,
        .used=used,.peak=maximum,.retained=atomic_load(&next->retained),.units=atomic_load(&next->units),
        .units_peak=atomic_load(&next->units_peak),.retained_units=atomic_load(&next->retained_units)};
    memcpy(out->name,next->name,64u);return FX_OK;
}
FxResult fx_account_destroy(FxAccountState *s) {
    if(!s || !s->generation)return FX_ARGUMENT;
    FxAccountRow *p=atomic_load_explicit(&s->rows,memory_order_acquire);
    for(FxAccountRow *q=p;q;q=q->next)if(atomic_load(&q->used) || atomic_load(&q->units) || atomic_load(&q->retained) || atomic_load(&q->retained_units))return FX_BUSY;
    while(p){FxAccountRow *next=p->next;
        if(p->flags&FX_ACCOUNT_STATIC)memset(p,0,sizeof(*p));
        else {if(!subtract(&s->retained,sizeof(*p)))(void)invalid(s);release_row(s,p);}
        p=next;
    }
    memset(s,0,sizeof(*s));return FX_OK;
}
FxResult fx_account_resource(void *context,uint32_t op,const FxNamedBinding *binding,FxPlatformOwner *owner,
    uint64_t token,uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)binding;(void)token;
    if(!owner || !cookie)return FX_ARGUMENT;
    FxAccountState *s=owner->accounting;
    if(op==FX_RESOURCE_ATTACH) {
        if(!s || !s->generation)return FX_CAPABILITY;
        *cookie=(uintptr_t)s;return FX_OK;
    }
    if(!s || *cookie!=(uintptr_t)s)return FX_STALE;
    if(op==FX_RESOURCE_DETACH){*cookie=0u;return FX_OK;}
    if(op==FX_RESOURCE_PANIC)return FX_OK; /* References/counters stay alive. */
    if(op!=FX_RESOURCE_CALL || !q || !a)return FX_ARGUMENT;
    if(q->bytes)return FX_ARGUMENT;
    uint64_t operation=q->arg[2];
    if(operation==FX_ACCOUNT_SHARED_QUERY || operation==FX_ACCOUNT_SHARED_ENUM) {
        s=s->shared;if(!s || !s->generation)return FX_CAPABILITY;
    }
    if(operation==FX_ACCOUNT_QUERY || operation==FX_ACCOUNT_SHARED_QUERY) {
        if(q->arg[3])return FX_ARGUMENT;
        FxAccountInfo info;fx_account_info(s,&info);
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(operation==FX_ACCOUNT_ENUM || operation==FX_ACCOUNT_SHARED_ENUM) {
        FxAccountRowInfo info;FxResult r=fx_account_next(s,q->arg[3],&info);
        if(r==FX_OK){memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);a->value[0]=info.id;}
        return r;
    }
    return FX_ARGUMENT;
}
