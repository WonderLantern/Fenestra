#if !defined(FX_ZF_C301_EXPERIMENTAL_SERVICES) || FX_ZF_C301_EXPERIMENTAL_SERVICES != 1
#error "Zf services wrapper is experimental and is not enabled by a production builder"
#endif

#include "services.h"
#include "fenestra_platform_native.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_memory.h"
#include "fenestra_accounting_service.h"
#include "fenestra_shared_accounts.h"

static void *account_allocate(void *context,uint32_t bytes) {
    const FxNamedBinding *binding=fx_platform_find(context,"rtos.memory.allocate");
    return binding?((void *(*)(uint32_t,uint32_t))(uintptr_t)binding->address)(bytes,0u):0;
}
static void account_release(void *context,void *memory) {
    const FxNamedBinding *binding=fx_platform_find(context,"rtos.memory.release");
    if(binding)((void (*)(void *))(uintptr_t)binding->address)(memory);
}

static FxResult initialize(FxZfServices *s,uint32_t owner,uint32_t generation,uint32_t capabilities,
                           uint32_t worker_protocol) {
    if (!s) return FX_ARGUMENT;
    if(atomic_load_explicit(&s->loaded.registry,memory_order_acquire))return FX_STATE;
    FxResult result=fx_platform_init(&s->platform, fx_platform_target(), owner, generation, capabilities);
    if(result!=FX_OK)return result;
    /* The same retained runtime ledger as the original Z9 service. Bind
     * observers before any service allocates binding/resource metadata. */
    result=fx_runtime_accounts_init(&s->accounting,owner,generation,
        (FxIndexMemory){&s->platform,account_allocate,account_release});
    if(result!=FX_OK)return result;
    s->platform.accounting=&s->accounting.state;
#ifdef FX_PLATFORM_PROFILE
    s->accounting.state.shared=fx_native_shared_accounts();
    s->platform.capacity=fx_native_capacity_registry();
#endif
    fx_account_owner_bind(&s->platform);
    /* Register the owner before any module callback can publish a scene or
     * UI change. The dispatcher retains the owner for the camera UI task. */
    result=fx_ui_dispatch_register(fx_native_ui_dispatch_domain(),&s->platform);
    if(result!=FX_OK)return result;
    if(worker_protocol)return FX_OK;
    /* Reuse the same copied-message protocol as the Z9 worker. The platform
     * backend alone does not fill sequence/result or handle SDK core events. */
    fx_service_init(&s->protocol,owner,generation,capabilities,
        (FxServiceBackend){s,fx_zf_service_call,fx_zf_service_stop});
    (void)fx_worker_wait_init(&s->wait,s->platform.platform,33u);
    result=fx_worker_wait_bind(&s->platform,&s->wait);
    if(result!=FX_OK)return result;
    s->protocol.wake_context=&s->wait;
    s->protocol.wake_callback=fx_worker_wait_signal;
    memset(&s->exchange,0,sizeof(s->exchange));
    memset(&s->module,0,sizeof(s->module));
    s->module.base=(FxModuleServices){FX_ABI,sizeof(s->module),0,0};
    s->module.service_abi=FX_SERVICE_ABI;
    s->module.owner=owner;
    s->module.generation=generation;
    s->module.capabilities=capabilities;
    s->module.exchange=&s->exchange;
    s->dispatched_sequence=0u;
    return FX_OK;
}
FxResult fx_zf_services_init(FxZfServices *s,uint32_t owner,uint32_t generation,uint32_t capabilities) {
    return initialize(s,owner,generation,capabilities,0u);
}
FxResult fx_zf_services_init_worker(FxZfServices *s,uint32_t owner,uint32_t generation,uint32_t capabilities) {
    return initialize(s,owner,generation,capabilities,1u);
}

FxResult fx_zf_services_prepare(FxZfServices *s, uint64_t now,uint32_t event,uint32_t value) {
    if(!s || !s->platform.platform)return FX_ARGUMENT;
    s->module.base.abi=FX_ABI;
    s->module.base.bytes=sizeof(s->module);
    s->module.base.context=0;
    s->module.base.log=0;
    s->module.time_ms=now;
    s->module.exchange=&s->exchange;
    s->module.event=event;
    s->module.event_value=value;
    s->protocol.time_ms=now;
    if(event==FX_EVENT_INIT || event==FX_EVENT_STOP)
        return fx_service_event(&s->protocol,event,value);
    return FX_OK;
}

FxResult fx_zf_services_dispatch_pending(FxZfServices *s) {
    if(!s || !s->platform.platform)return FX_ARGUMENT;
    const FxServiceRequest *request=&s->exchange.request;
    if(!request->sequence || request->sequence==s->dispatched_sequence)return FX_EMPTY;
    FxServiceReply reply={0};
    uint64_t previous=s->protocol.last_sequence;
    (void)fx_service_dispatch(&s->protocol,request,&reply);
    s->pacing=fx_worker_pace_reply(&s->pace,previous,request->sequence,&reply);
    s->exchange.reply=reply;
    s->dispatched_sequence=request->sequence;
    /* EMPTY/BUSY/CAPABILITY are module-visible replies, not worker faults.
     * Publish them with their request sequence so SDK retries can proceed. */
    return s->protocol.fault?FX_INTEGRITY:FX_OK;
}

FxResult fx_zf_service_call(void *context, const FxServiceRequest *request, FxServiceReply *reply) {
    FxZfServices *s = context;
    if (!s) return FX_ARGUMENT;
    uint64_t before=fx_platform_ui_stamp(&s->platform);
    FxResult result=fx_platform_dispatch(&s->platform, request, reply);
    if(fx_platform_ui_changed(&s->platform,before))
        fx_native_ui_refresh_owner(&s->platform);
    return result;
}

FxResult fx_zf_service_stop(void *context, uint32_t panic) {
    FxZfServices *s = context;
    if (!s) return FX_ARGUMENT;
    return fx_platform_stop(&s->platform, panic);
}
FxResult fx_zf_services_stop(FxZfServices *s,uint32_t panic) {
    if(!s)return FX_ARGUMENT;
    if(panic || s->protocol.fault) {
        fx_service_fault(&s->protocol);
        return s->protocol.stop_state==FX_SERVICE_STOP_FAULT_PENDING?FX_BUSY:FX_OK;
    }
    return fx_service_stop(&s->protocol);
}
FxResult fx_zf_services_bind_module(FxZfServices *s,const char *id,const FxModule *module,
                                   void *image,uint32_t bytes) {
    if(!s)return FX_ARGUMENT;
    FxZfLoadedModules *registry=fx_zf_owner_modules();if(!registry)return FX_STATE;
    return fx_zf_modules_bind(registry,&s->loaded,id,&s->platform,module,image,bytes);
}
