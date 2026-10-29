#include "fenestra_tasks.h"
#include "fenestra_memory.h"
/* Stable owner roots retain sessions. Only enqueue/claim/retire hold the
 * domain lock; allocation and native calls never do. Completion does not
 * reacquire the lock from the higher-priority original UI task. */
typedef struct FxTaskOwner FxTaskOwner;
struct FxTaskSession {
    FxTaskOwner *group;
    FxTaskSession *all_next,*free_next,*previous,*next;
    FxTaskDomain *domain;FxPlatformOwner *owner;
    uint64_t token,ticket;
    _Atomic uint32_t state;
    uint32_t call_bytes,enqueued;
    FxResult result;uintptr_t task;
    uint64_t values[2];FxNamedBinding binding;FxNativeCallFrame call;
};
struct FxTaskOwner {
    FxProviderContext retained;FxTaskDomain *domain;
    FxTaskSession *all,*free;
    FxAllocationObserver observer;
};
static int enter(FxTaskDomain *d) {
    uint32_t zero=0u;
    return atomic_compare_exchange_strong_explicit(&d->lock,&zero,1u,memory_order_acquire,memory_order_relaxed);
}
static void leave(FxTaskDomain *d){atomic_store_explicit(&d->lock,0u,memory_order_release);}
static int reserve(FxTaskDomain *d,uint32_t bytes) {
    uint32_t used=atomic_load_explicit(&d->requested_bytes,memory_order_relaxed);
    const uint32_t limit=FX_TASK_BUDGET-(uint32_t)sizeof(*d);
    while(used<=limit && bytes<=limit-used) {
        if(atomic_compare_exchange_weak_explicit(&d->requested_bytes,&used,used+bytes,memory_order_acq_rel,memory_order_relaxed)) {
            uint32_t peak=atomic_load_explicit(&d->peak_bytes,memory_order_relaxed);
            while(peak<used+bytes && !atomic_compare_exchange_weak_explicit(&d->peak_bytes,&peak,used+bytes,memory_order_relaxed,memory_order_relaxed)){}
            return 1;
        }
    }
    return 0;
}
static FxTaskOwner *group(FxPlatformOwner *owner,FxTaskDomain *domain) {
    for(FxProviderContext *p=atomic_load_explicit(&owner->provider_contexts,memory_order_acquire);p;p=p->next) {
        uint32_t n=0u;while(n<sizeof("core.tasks") && p->name[n]=="core.tasks"[n])++n;
        if(n==sizeof("core.tasks")) {FxTaskOwner *g=p->context;if(g->domain==domain)return g;}
    }
    return 0;
}
static void remove_job(FxTaskSession *s) {
    if(!s->enqueued)return;
    FxTaskDomain *d=s->domain;
    if(s->previous)s->previous->next=s->next;
    else atomic_store_explicit(&d->head,(uintptr_t)s->next,memory_order_release);
    if(s->next)s->next->previous=s->previous;else d->tail=(uintptr_t)s->previous;
    s->previous=s->next=0;s->enqueued=0u;--d->queued;
}
static void enqueue(FxTaskSession *s) {
    FxTaskDomain *d=s->domain;s->next=0;s->previous=(void *)d->tail;s->enqueued=1u;
    if(s->previous)s->previous->next=s;else atomic_store_explicit(&d->head,(uintptr_t)s,memory_order_release);
    d->tail=(uintptr_t)s;++d->queued;if(d->queued>d->queued_peak)d->queued_peak=d->queued;
}
FxResult fx_tasks_capture(FxTaskDomain *d,uintptr_t task) {
    if(!d || !task)return FX_ARGUMENT;
    uintptr_t zero=0u;
    if(atomic_compare_exchange_strong_explicit(&d->task,&zero,task,memory_order_release,memory_order_acquire) || zero==task)return FX_OK;
    return FX_STATE;
}
int fx_tasks_pending(const FxTaskDomain *d) {
    return d && atomic_load_explicit(&d->head,memory_order_acquire)!=0u;
}
FxResult fx_tasks_attach(FxTaskDomain *d,FxPlatformOwner *owner,uint64_t token,
    void *(*allocate)(void *,uint32_t),void (*release)(void *,void *),void *context,FxTaskSession **out) {
    if(!d || !owner || !token || !allocate || !release || !out)return FX_ARGUMENT;
    /* Caller holds owner.binding_lock, including publication and rollback. */
    FxTaskOwner *g=group(owner,d);
    FxAllocationObserver observer=g?g->observer:d->owner_account?d->owner_account(owner):(FxAllocationObserver){0};
    if(!enter(d))return FX_BUSY;
    if(g && g->free) {
        FxTaskSession *s=g->free;g->free=s->free_next;s->free_next=0;s->token=token;s->state=FX_TASK_IDLE;
        ++d->active_sessions;*out=s;leave(d);return FX_OK;
    }
    leave(d);
    uint32_t bytes=(uint32_t)sizeof(FxTaskSession)+(g?0u:(uint32_t)sizeof(FxTaskOwner));
    if(!reserve(d,bytes))return FX_BUDGET;
    FxTaskOwner *created=0;FxTaskSession *s=allocate(context,(uint32_t)sizeof(*s));
    FxResult result=FX_BUDGET;
    if(!s)goto failed;
    fx_allocation_observe(observer,(uint32_t)sizeof(*s),FX_ALLOC_REQUESTED);
    memset(s,0,sizeof(*s));
    if(!g) {
        created=allocate(context,(uint32_t)sizeof(*created));if(!created)goto failed;
        fx_allocation_observe(observer,(uint32_t)sizeof(*created),FX_ALLOC_REQUESTED);
        memset(created,0,sizeof(*created));created->domain=d;created->observer=observer;created->retained=(FxProviderContext){0,"core.tasks",created};g=created;
    }
    s->domain=d;s->owner=owner;s->group=g;s->token=token;
    if(!enter(d)){result=FX_BUSY;goto failed;}
    if(created) {
        result=fx_platform_retain_context(owner,&created->retained);
        if(result!=FX_OK){leave(d);goto failed;}
        fx_allocation_observe(observer,(uint32_t)sizeof(*created),FX_ALLOC_PUBLISHED);
        ++d->owner_groups;
    }
    fx_allocation_observe(observer,(uint32_t)sizeof(*s),FX_ALLOC_PUBLISHED);
    s->all_next=g->all;g->all=s;++d->sessions;++d->active_sessions;d->retained_bytes+=bytes;
    *out=s;leave(d);return FX_OK;
failed:
    if(created){release(context,created);fx_allocation_observe(observer,(uint32_t)sizeof(*created),FX_ALLOC_FREE_PENDING);}
    if(s){release(context,s);fx_allocation_observe(observer,(uint32_t)sizeof(*s),FX_ALLOC_FREE_PENDING);}
    atomic_fetch_sub_explicit(&d->requested_bytes,bytes,memory_order_release);return result;
}
FxResult fx_tasks_poll(FxTaskDomain *d,uintptr_t task) {
    if(!d || !task || task!=atomic_load_explicit(&d->task,memory_order_acquire))return FX_EMPTY;
    if(!enter(d))return FX_BUSY;
    if(atomic_load_explicit(&d->running,memory_order_relaxed)){leave(d);return FX_BUSY;}
    FxTaskSession *s=(void *)atomic_load_explicit(&d->head,memory_order_relaxed);
    if(!s){leave(d);return FX_EMPTY;}
    remove_job(s);s->state=FX_TASK_RUNNING;s->task=task;FxPlatformOwner *owner=s->owner;
    atomic_store_explicit(&d->running,1u,memory_order_relaxed);leave(d);
    FxResult r=owner->phase==FX_OWNER_RUNNING?
        fx_platform_invoke_native(owner,&s->binding,&s->call,s->call_bytes,s->values):FX_STATE;
    s->result=r;
    uint32_t completed=atomic_load_explicit(&d->completed,memory_order_relaxed);
    if(completed!=UINT32_MAX)atomic_store_explicit(&d->completed,completed+1u,memory_order_relaxed);
    atomic_store_explicit(&s->state,FX_TASK_DONE,memory_order_release);
    atomic_store_explicit(&d->running,0u,memory_order_release);
    fx_platform_wake(owner);return FX_OK;
}
FxResult fx_tasks_resource(FxTaskSession *s,uint32_t op,FxPlatformOwner *owner,uint64_t token,
    const FxServiceRequest *q,FxServiceReply *a) {
    if(!s || s->owner!=owner || s->token!=token)return FX_STALE;
    FxTaskDomain *d=s->domain;if(!enter(d))return FX_BUSY;
    FxResult r=FX_OK;
    if(op==FX_RESOURCE_PANIC || op==FX_RESOURCE_DETACH) {
        if(s->state==FX_TASK_RUNNING)r=FX_BUSY;
        else if(s->state!=FX_TASK_CLOSED) {
            remove_job(s);s->state=FX_TASK_CLOSED;--d->active_sessions;
            if(s->ticket!=UINT64_MAX){s->free_next=s->group->free;s->group->free=s;}
        }
    } else if(op!=FX_RESOURCE_CALL || !q || !a || s->state==FX_TASK_CLOSED)r=FX_STATE;
    else if(q->arg[2]==FX_TASK_POST) {
        if(q->bytes!=sizeof(FxTaskPost) || q->arg[3])r=FX_ARGUMENT;
        else if(s->state!=FX_TASK_IDLE)r=FX_BUSY;
        else if(s->ticket==UINT64_MAX)r=FX_BUDGET;
        else {
            FxTaskPost p;memcpy(&p,q->data,sizeof(p));
            static const char context[FX_TASK_CONTEXT_BYTES]="original.ui";
            if(memcmp(p.context,context,sizeof(context)) || p.reserved)r=FX_ARGUMENT;
            else {
                FxNamedBinding b;r=fx_platform_prepare_native(owner,p.binding,p.call,p.call_bytes,&b);
                if(r==FX_OK) {
                    s->binding=b;s->call_bytes=p.call_bytes;memset(&s->call,0,sizeof(s->call));memcpy(&s->call,p.call,p.call_bytes);
                    s->values[0]=s->values[1]=0u;s->task=0u;s->result=FX_BUSY;++s->ticket;s->state=FX_TASK_QUEUED;
                    enqueue(s);a->value[0]=s->ticket;
                }
            }
        }
    } else if(q->bytes)r=FX_ARGUMENT;
    else if(q->arg[2]==FX_TASK_QUERY) {
        if(q->arg[3])r=FX_ARGUMENT;
        else {FxTaskInfo info={.abi=1u,.bytes=sizeof(info),.operations=63u,.capacity=0u,
                .task=atomic_load_explicit(&d->task,memory_order_acquire),.context="original.ui"};
            memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);}
    } else if(q->arg[2]==FX_TASK_CAPACITY) {
        if(q->arg[3])r=FX_ARGUMENT;
        else {
            uint32_t requested=atomic_load_explicit(&d->requested_bytes,memory_order_acquire);
            uint32_t peak=atomic_load_explicit(&d->peak_bytes,memory_order_relaxed);if(peak<requested)peak=requested;
            FxTaskCapacity c={.abi=1u,.bytes=sizeof(c),.budget=FX_TASK_BUDGET,.used=(uint32_t)sizeof(*d)+requested,
                .peak=(uint32_t)sizeof(*d)+peak,.reserved_bytes=requested-d->retained_bytes,
                .owner_groups=d->owner_groups,.sessions=d->sessions,.active_sessions=d->active_sessions,
                .queued=d->queued,.queued_peak=d->queued_peak,.running=atomic_load(&d->running),.completed=atomic_load(&d->completed),
                .session_bytes=sizeof(*s),.domain_bytes=sizeof(*d)};
            memcpy(a->data,&c,sizeof(c));a->bytes=sizeof(c);
        }
    } else if(!q->arg[3] || q->arg[3]!=s->ticket || s->state==FX_TASK_IDLE)r=FX_STALE;
    else if(q->arg[2]==FX_TASK_STATUS) {
        uint32_t state=atomic_load_explicit(&s->state,memory_order_acquire);
        FxTaskStatus status={1u,sizeof(status),s->ticket,state,FX_BUSY,s->task,{0u,0u}};
        if(state==FX_TASK_DONE){status.result=(uint32_t)s->result;status.value[0]=s->values[0];status.value[1]=s->values[1];}
        memcpy(a->data,&status,sizeof(status));a->bytes=sizeof(status);
    } else if(q->arg[2]==FX_TASK_FETCH) {
        if(s->state!=FX_TASK_DONE)r=FX_BUSY;
        else {a->value[0]=s->ticket;a->value[1]=s->result;a->value[2]=s->values[0];a->value[3]=s->values[1];
            if(s->binding.abi==FX_CALL_AAPCS64){memcpy(a->data,&s->call,sizeof(s->call));a->bytes=sizeof(s->call);}
            s->state=FX_TASK_IDLE;}
    } else if(q->arg[2]==FX_TASK_CANCEL) {
        if(s->state==FX_TASK_RUNNING)r=FX_BUSY;
        else {remove_job(s);s->state=FX_TASK_IDLE;}
    } else r=FX_ARGUMENT;
    leave(d);return r;
}
