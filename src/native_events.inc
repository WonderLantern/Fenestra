/* Included by native_resources.c: the same owner lock and named bindings
 * serve both targets. No native event is created before an explicit CREATE. */
int64_t fx_native_event_signal(uintptr_t cookie,uint32_t bits) {
    Event *e=(Event *)cookie;
    int32_t irq=((int32_t (*)(void))e->context)();
    uintptr_t fn=irq?e->set_irq:e->set;
    int32_t code=((int32_t (*)(int32_t,uint32_t))fn)(e->id,bits);
    if(!code && bits)fx_platform_wake(e->owner);
    return code;
}
static FxResult event_error(FxServiceReply *a,int32_t code) {
    a->value[3]=(uint64_t)(int64_t)code;
    return code==0?FX_OK:code==-50?FX_EMPTY:FX_IO;
}
static FxResult event_delete(FxPlatformOwner *o,Resources *s,Event *e,FxServiceReply *a) {
    uintptr_t fn=native(o,"rtos.event.delete");if(!fn)return FX_CAPABILITY;
    int32_t code=((int32_t (*)(int32_t))fn)(e->id);
    if(a)a->value[3]=(uint64_t)(int64_t)code;
    fx_capacity_release(&e->claim,(uint32_t)code);
    if(code){(void)retain_object(&s->event,e->token,s->events,0u);return FX_IO;}
    return release_object(&s->event,e->token,s->events,0u);
}
static FxResult event_call(FxPlatformOwner *o,Resources *s,const FxServiceRequest *q,FxServiceReply *a) {
    uint64_t op=q->arg[2];
    if(op==FX_EVENT_QUERY) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        FxEventInfo info={.abi=1u,.bytes=sizeof(info),.operations=0x1feu,.capacity=fx_slot_pool_limit(&s->event),.modes=0x33u};
        info.retained=s->event.retained;info.active=s->event.used-s->event.retained;
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(q->bytes!=sizeof(FxEventRequest) || q->arg[3])return FX_ARGUMENT;
    FxEventRequest r;memcpy(&r,q->data,sizeof(r));
    if(op==FX_EVENT_CREATE) {
        if(r.handle || r.mode)return FX_ARGUMENT;

        uintptr_t create=native(o,"rtos.event.create"),context=native(o,"rtos.context.is-irq"),
            set=native(o,"rtos.event.set"),set_irq=native(o,"rtos.event.set-irq");
        if(!create || !context || !set || !set_irq || !native(o,"rtos.event.delete") ||
           !native(o,"rtos.event.clear-keep") || !native(o,"rtos.event.wait"))return FX_CAPABILITY;
        const uint32_t configuration[2]={2u,r.bits};
        void *item=0;uint64_t handle=0u;FxResult result=resource_slot(o,&s->event,&item,&handle);Event *e=item;
        if(result!=FX_OK)return result;
        int32_t id=((int32_t (*)(const uint32_t *))create)(configuration);
        FxCapacityDomain *domain=fx_capacity_find(o->capacity,"rtos.events");
        if(id<=0){fx_capacity_failed(domain,(uint32_t)id);(void)fx_slot_pool_release(&s->event,handle);return event_error(a,id?id:-42);}
        *e=(Event){.id=id,.token=handle,.context=context,.set=set,.set_irq=set_irq,.owner=o,.claim=fx_capacity_acquire(domain,1u)};
        acquired(s->events,0u);
        a->value[0]=e->token;return FX_OK;
    }
    Event *e=fx_slot_pool_get(&s->event,r.handle);
    if(!e)return FX_STALE;
    if(op==FX_EVENT_RECLAIM) {
        if(r.bits || r.mode)return FX_ARGUMENT;
        return event_delete(o,s,e,a);
    }
    if(e->released)return FX_STALE;
    if(op==FX_EVENT_RELEASE || op==FX_EVENT_EXPORT) {
        if(r.bits || r.mode)return FX_ARGUMENT;
        if(op==FX_EVENT_RELEASE) {
            if(!e->escaped)return event_delete(o,s,e,a);
            e->released=1u;fx_capacity_retain(&e->claim);return retain_object(&s->event,e->token,s->events,0u);
        }
        e->escaped=1u;a->value[0]=e->token;a->value[1]=(uintptr_t)e;a->value[2]=(uint32_t)e->id;return FX_OK;
    }
    if(op==FX_EVENT_SET) {
        if(r.mode)return FX_ARGUMENT;
        return event_error(a,(int32_t)fx_native_event_signal((uintptr_t)e,r.bits));
    }
    if(op==FX_EVENT_CLEAR) {
        if(r.mode)return FX_ARGUMENT;
        uintptr_t fn=native(o,"rtos.event.clear-keep");if(!fn)return FX_CAPABILITY;
        return event_error(a,((int32_t (*)(int32_t,uint32_t))fn)(e->id,~r.bits));
    }
    if(op==FX_EVENT_POLL) {
        uintptr_t fn=native(o,"rtos.event.wait");if(!fn)return FX_CAPABILITY;
        uint32_t bits=0u;
        int32_t code=((int32_t (*)(int32_t,uint32_t,uint32_t,uint32_t *,int32_t))fn)(e->id,r.bits,r.mode,&bits,0);
        if(!code)a->value[0]=bits;
        return event_error(a,code);
    }
    return FX_ARGUMENT;
}
