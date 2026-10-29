#include "fenestra_worker_wait.h"
#include "fenestra_memory.h"
static uintptr_t function(const FxPlatform *p,const char *name) {
    for(uint32_t i=0u;i<p->count;++i) {
        const FxNamedBinding *b=&p->bindings[i];uint32_t n=0u;
        while(name[n] && name[n]==b->name[n])++n;
        if(!name[n] && !b->name[n] && b->kind==FX_BIND_FUNCTION && b->abi==FX_CALL_U64_8)return (uintptr_t)b->address;
    }
    return 0;
}
FxResult fx_worker_wait_init(FxWorkerWait *s,const FxPlatform *p,uint32_t period) {
    if(!s || !period || (p && !p->bindings))return FX_ARGUMENT;
    if(s->initialized)return FX_BUSY;
    memset(s,0,sizeof(*s));s->period_ms=period;atomic_store(&s->epoch,1u);s->initialized=1u;
    return p?fx_worker_wait_profile(s,p):FX_CAPABILITY;
}
FxResult fx_worker_wait_profile(FxWorkerWait *s,const FxPlatform *p) {
    if(!s || !s->initialized || !p || !p->bindings)return FX_ARGUMENT;
    if(atomic_load_explicit(&s->event,memory_order_acquire))return FX_BUSY;
    s->create=function(p,"rtos.event.create");s->set=function(p,"rtos.event.set");
    s->set_irq=function(p,"rtos.event.set-irq");s->clear=function(p,"rtos.event.clear-keep");
    s->wait=function(p,"rtos.event.wait");s->is_irq=function(p,"rtos.context.is-irq");
    return s->create && s->set && s->set_irq && s->clear && s->wait && s->is_irq?FX_OK:FX_CAPABILITY;
}
FxResult fx_worker_wait_bind(FxPlatformOwner *o,FxWorkerWait *s) {
    if(!o || !s || !s->initialized)return FX_ARGUMENT;
    if(atomic_load_explicit(&o->wake_callback,memory_order_acquire))return FX_BUSY;
    o->wake_context=s;
    s->event_capacity=fx_capacity_find(o->capacity,"rtos.events");
    atomic_store_explicit(&o->wake_callback,(uintptr_t)fx_worker_wait_signal,memory_order_release);return FX_OK;
}
uint64_t fx_worker_wait_ticket(const FxWorkerWait *s){return s?atomic_load_explicit(&s->epoch,memory_order_acquire):0u;}
static FxResult error(FxWorkerWait *s,int32_t code) {
    atomic_store_explicit(&s->native_error,code,memory_order_relaxed);
    atomic_store_explicit(&s->error,FX_IO,memory_order_release);return FX_IO;
}
void fx_worker_wait_signal(void *context) {
    FxWorkerWait *s=context;if(!s || !s->initialized)return;
    uint64_t ticket=atomic_load_explicit(&s->epoch,memory_order_acquire);
    for(;;) {
        if(ticket==UINT64_MAX){atomic_store_explicit(&s->error,FX_BUDGET,memory_order_release);break;}
        if(atomic_compare_exchange_weak_explicit(&s->epoch,&ticket,ticket+1u,memory_order_acq_rel,memory_order_acquire))break;
    }
    atomic_fetch_add_explicit(&s->signals,1u,memory_order_relaxed);
    int32_t id=atomic_load_explicit(&s->event,memory_order_acquire);if(id<=0)return;
    int32_t irq=((int32_t (*)(void))s->is_irq)();uintptr_t fn=irq?s->set_irq:s->set;
    int32_t code=((int32_t (*)(int32_t,uint32_t))fn)(id,1u);if(code)(void)error(s,code);
}
FxResult fx_worker_wait_step(FxWorkerWait *s,uint64_t ticket,uint32_t ticks,uint32_t *expired) {
    if(!s || !s->initialized || !expired || (ticks>INT32_MAX && ticks!=UINT32_MAX))return FX_ARGUMENT;
    *expired=0u;
    if(!ticks || !ticket || fx_worker_wait_ticket(s)!=ticket)return FX_OK;
    uint32_t failed=atomic_load_explicit(&s->error,memory_order_acquire);if(failed)return (FxResult)failed;
    if(!s->create || !s->set || !s->set_irq || !s->clear || !s->wait || !s->is_irq)return FX_CAPABILITY;
    int32_t id=atomic_load_explicit(&s->event,memory_order_acquire);
    if(id<=0) {
        const uint32_t configuration[2]={2u,0u};id=((int32_t (*)(const uint32_t *))s->create)(configuration);
        if(id<=0){fx_capacity_failed(s->event_capacity,(uint32_t)id);return error(s,id?id:-42);}
        s->event_claim=fx_capacity_acquire(s->event_capacity,1u);fx_capacity_retain(&s->event_claim);
        atomic_store_explicit(&s->event,id,memory_order_release);
    }
    /* Clear BEFORE checking the ticket. A producer before clear changes the
     * ticket; a producer after check leaves the native flag/wait sticky. */
    int32_t code=((int32_t (*)(int32_t,uint32_t))s->clear)(id,~1u);if(code)return error(s,code);
    if(fx_worker_wait_ticket(s)!=ticket)return FX_OK;
    uint32_t bits=0u;atomic_fetch_add_explicit(&s->sleeps,1u,memory_order_relaxed);
    code=((int32_t (*)(int32_t,uint32_t,uint32_t,uint32_t *,int32_t))s->wait)(id,1u,0x21u,&bits,ticks==UINT32_MAX?-1:(int32_t)ticks);
    if(code==-50){*expired=1u;atomic_fetch_add_explicit(&s->timeouts,1u,memory_order_relaxed);return FX_OK;}
    return code?error(s,code):FX_OK;
}
FxResult fx_worker_wait_continue(FxWorkerWait *s,uint32_t quantum,FxServiceReply *reply,FxServiceOwner *service) {
    if(!s || !s->armed || !reply)return FX_ARGUMENT;
    uint32_t expired=0u,wait=s->remaining;
    if(quantum && (wait==UINT32_MAX || wait>quantum))wait=quantum;
    FxResult r=fx_worker_wait_step(s,s->observed,wait,&expired);
    if(r==FX_OK && expired && s->remaining!=UINT32_MAX)s->remaining-=wait;
    uint64_t ticket=fx_worker_wait_ticket(s);
    if(r==FX_OK && s->observed && ticket==s->observed && s->remaining)return FX_EMPTY;
    s->armed=0u;s->resumed=1u;s->timed_out=(uint32_t)(r==FX_OK && ticket==s->observed && !s->remaining);
    reply->result=(uint32_t)r;reply->value[0]=ticket;reply->value[1]=(uint32_t)(ticket!=s->observed);
    reply->value[2]=s->timed_out;reply->value[3]=(uint64_t)(int64_t)atomic_load(&s->native_error);
    /* A completed asynchronous request replaces its provisional dispatch
     * reply. An identical sequence retry must replay this final result. */
    if(service)service->last_reply=*reply;
    return r;
}
FxResult fx_schedule_resource(uint32_t op,FxPlatformOwner *o,uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    if(!o || !cookie)return FX_ARGUMENT;
    if(atomic_load_explicit(&o->wake_callback,memory_order_acquire)!=(uintptr_t)fx_worker_wait_signal)return FX_CAPABILITY;
    FxWorkerWait *s=o->wake_context;if(!s || !s->initialized)return FX_CAPABILITY;
    if(op==FX_RESOURCE_ATTACH){*cookie=(uintptr_t)s;return FX_OK;}
    if(*cookie!=(uintptr_t)s)return FX_STALE;
    if(op==FX_RESOURCE_DETACH){*cookie=0u;return FX_OK;}
    if(op==FX_RESOURCE_PANIC)return FX_OK;
    if(op!=FX_RESOURCE_CALL || !q || !a || q->arg[3])return FX_ARGUMENT;
    if(q->arg[2]==FX_SCHEDULE_QUERY) {
        if(q->bytes)return FX_ARGUMENT;
        FxScheduleInfo info={1u,sizeof(info),6u,7u,INT32_MAX,s->period_ms,(uint32_t)(atomic_load(&s->event)>0),0u};
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);a->value[0]=fx_worker_wait_ticket(s);
        a->value[1]=atomic_load(&s->error);a->value[3]=(uint64_t)(int64_t)atomic_load(&s->native_error);return FX_OK;
    }
    if(q->arg[2]!=FX_SCHEDULE_WAIT || q->bytes!=sizeof(FxScheduleWait))return FX_ARGUMENT;
    FxScheduleWait r;memcpy(&r,q->data,sizeof(r));
    if(r.reserved || (r.timeout_ticks>INT32_MAX && r.timeout_ticks!=UINT32_MAX))return FX_ARGUMENT;
    if(s->armed)return FX_BUSY;
    s->observed=r.observed;s->remaining=r.timeout_ticks;s->timed_out=0u;s->armed=1u;return FX_OK;
}
const uint32_t fx_worker_wait_metadata[]={0x46585753u,1u,offsetof(FxPlatformOwner,wake_context),sizeof(FxWorkerWait),
    offsetof(FxWorkerWait,epoch),offsetof(FxWorkerWait,event),offsetof(FxWorkerWait,error),
    offsetof(FxWorkerWait,native_error),offsetof(FxWorkerWait,armed),offsetof(FxWorkerWait,remaining),
    offsetof(FxWorkerWait,sleeps),offsetof(FxWorkerWait,signals),offsetof(FxWorkerWait,timeouts),offsetof(FxWorkerWait,resumed)};
