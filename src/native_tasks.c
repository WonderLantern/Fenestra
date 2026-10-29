#include "fenestra_tasks.h"
#include "fenestra_native_ui_dispatch.h"
static void *allocate(void *context,uint32_t bytes) {
    const FxNamedBinding *b=fx_platform_find(context,"rtos.memory.allocate");
    if(!b || b->kind!=FX_BIND_FUNCTION || b->abi!=FX_CALL_U64_8)return 0;
    return ((void *(*)(uint32_t,uint32_t))(uintptr_t)b->address)(bytes,0u);
}
static void release(void *context,void *allocation) {
    const FxNamedBinding *b=fx_platform_find(context,"rtos.memory.release");
    ((void (*)(void *))(uintptr_t)b->address)(allocation);
}
FxResult fx_native_tasks_resource(void *context,uint32_t op,const FxNamedBinding *b,FxPlatformOwner *owner,
    uint64_t token,uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)b;
    if(!owner || !cookie)return FX_ARGUMENT;
    FxNativeUiDispatch *d=fx_native_ui_dispatch_domain();if(!d)return FX_STATE;
    if(op==FX_RESOURCE_ATTACH) {
        const FxNamedBinding *free_binding=fx_platform_find(owner,"rtos.memory.release");
        if(!free_binding || free_binding->kind!=FX_BIND_FUNCTION || free_binding->abi!=FX_CALL_U64_8)return FX_CAPABILITY;
        FxTaskSession *s=0;FxResult r=fx_tasks_attach(&d->tasks,owner,token,allocate,release,owner,&s);
        if(r==FX_OK)*cookie=(uintptr_t)s;
        return r;
    }
    void *group=0;
    if(op==FX_RESOURCE_CALL && q && q->arg[2]==FX_TASK_POST) {
        group=((void *(*)(void))fx_native_ui_dispatch_profile.event_group)();
        if(!group || !*(const uintptr_t *)group)return FX_STATE;
    }
    FxResult r=fx_tasks_resource((void *)(uintptr_t)*cookie,op,owner,token,q,a);
    /* Publish before the original coalescing wake signal. The UI may already
     * be inside an infinite original wait, before our pending-job timeout
     * limit could be observed. No key/message is inserted into its queues. */
    if(r==FX_OK && group)
        ((void (*)(void *,uint32_t))fx_native_ui_dispatch_profile.event_set)(group,fx_native_ui_dispatch_profile.wake_mask);
    if(r==FX_OK && op==FX_RESOURCE_DETACH)*cookie=0u;
    return r;
}
