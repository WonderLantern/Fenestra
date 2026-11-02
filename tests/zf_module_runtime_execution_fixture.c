#include "../src/platform/zf_c301/module_runtime.h"
#include <assert.h>

static uint32_t activate_calls,invoke_calls;
static uint32_t observed_events[4],observed_event_count;
static uint32_t dispatch_calls,dispatch_saw_request;
static uint32_t private_context;
static uint32_t interrupt_entry,prepare_calls,acquires,releases;

FxResult fx_zf_module_stage(FxZfModuleStage *stage,const FxZfCardNative *native,
    const char *native_path,const char *logical_path,uint32_t bytes,const uint8_t digest[32],
    uint64_t capabilities,uint8_t *file,uint32_t file_capacity,uint8_t *package,
    uint32_t package_capacity,uint8_t *image,uint32_t image_capacity,uint64_t now) {
    (void)stage;(void)native;(void)native_path;(void)logical_path;(void)bytes;(void)digest;
    (void)capabilities;(void)file;(void)file_capacity;(void)package;(void)package_capacity;
    (void)image;(void)image_capacity;(void)now;return FX_OK;
}
FxResult fx_zf_services_init(FxZfServices *services,uint32_t owner,uint32_t generation,uint32_t capabilities) {
    (void)services;(void)owner;(void)generation;(void)capabilities;return FX_OK;
}
FxResult fx_zf_services_bind_module(FxZfServices *services,const char *id,const FxModule *module,
                                    void *image,uint32_t bytes) {
    (void)services;(void)id;(void)module;(void)image;(void)bytes;return FX_OK;
}

FxResult fx_exec_live(const FxExecArena *arena) {
    if(arena && arena->published)assert(private_context);
    return arena && arena->bound ? FX_OK : FX_CAPABILITY;
}
static FxZfExecContext *bound_context;
static uint32_t gate_calls;
FxResult fx_zf_exec_call(FxZfExecContext *context,uintptr_t root,int (*operation)(FxZfExecContext *)) {
    assert(context && root && operation && context==bound_context);
    ++gate_calls;
    assert(!private_context);private_context=1u;
    FxResult result=(FxResult)operation(context);
    private_context=0u;return result;
}
int fx_zf_user_call(uintptr_t entry,const void *api,uintptr_t root,uintptr_t stack,FxZfExecContext *context) {
    (void)entry;(void)root;(void)stack;(void)context;
    const FxServiceModule *module=api;
    if(interrupt_entry) {
        --interrupt_entry;context->continuation[34]=1u;return 0;
    }
    context->continuation[34]=0u;
    assert(module && module->exchange && module->exchange!=(const FxServiceExchange *)api);
    module->exchange->request.sequence=1u;
    module->exchange->request.operation=FX_S_QUERY;
    module->exchange->request.generation=module->generation;
    module->exchange->request.bytes=0u;
    return 0;
}
FxResult fx_loader_activate_mapped(FxLoader *loader,FxExecArena *arena,uint64_t now) {
    (void)loader;(void)now;
    assert(arena && arena->bound && !arena->published);
    assert(private_context);
    ++activate_calls;arena->published=1u;return FX_OK;
}
FxResult fx_zf_services_prepare(FxZfServices *services,uint64_t now,uint32_t event,uint32_t value) {
    (void)now;(void)value;++prepare_calls;services->module.event=event;return FX_OK;
}
FxResult fx_loader_invoke(FxLoader *loader,uint32_t generation,const FxModuleServices *services,
                          uint64_t now,int *result) {
    (void)loader;(void)generation;(void)services;(void)now;
    observed_events[observed_event_count++]=((const FxServiceModule *)services)->event;
    ++invoke_calls;*result=7;return FX_OK;
}
FxResult fx_zf_services_dispatch_pending(FxZfServices *services) {
    ++dispatch_calls;
    dispatch_saw_request=services->exchange.request.sequence==1u &&
        services->exchange.request.operation==FX_S_QUERY;
    return dispatch_saw_request?FX_OK:FX_INTEGRITY;
}
FxResult fx_zf_services_stop(FxZfServices *context,uint32_t panic) {(void)context;(void)panic;return FX_OK;}
FxResult fx_execution_resume(FxRuntime *runtime,uint64_t now) {(void)runtime;(void)now;return FX_OK;}
FxResult fx_acquire(FxRuntime *runtime,uint32_t generation,uint64_t now,FxLease *lease) {
    (void)runtime;(void)generation;(void)now;++acquires;lease->id=1u;lease->generation=1u;return FX_OK;
}
FxResult fx_execution_idle(FxRuntime *runtime,uint64_t now) {(void)runtime;(void)now;return FX_OK;}
FxResult fx_release(FxRuntime *runtime,FxLease lease,uint64_t now) {(void)runtime;(void)lease;(void)now;++releases;return FX_OK;}
FxResult fx_disable(FxRuntime *runtime,uint64_t now,uint64_t timeout) {
    (void)now;(void)timeout;runtime->phase=FX_QUIESCING;return FX_OK;
}
FxResult fx_loader_confirm_quiesced(FxLoader *loader,uint32_t generation,uint64_t now) {
    (void)loader;(void)generation;(void)now;assert(private_context);return FX_OK;
}

int main(void) {
    FxZfModuleRuntime runtime={0};int result=0;
    runtime.phase=FX_ZF_MODULE_BOUND;
    assert(fx_zf_module_runtime_activate(&runtime,1u)==FX_CAPABILITY);
    assert(activate_calls==0u);
    runtime.phase=FX_ZF_MODULE_ACTIVE;
    assert(fx_zf_module_runtime_invoke(&runtime,2u,&result)==FX_CAPABILITY);
    assert(invoke_calls==0u);
    FxExecArena arena={0};arena.bound=1u;
    FxZfExecContext context={0};FxServiceModule api={0};
    api.generation=1u;
    bound_context=&context;
    runtime.phase=FX_ZF_MODULE_BOUND;
    assert(fx_zf_module_runtime_bind_execution(&runtime,&arena)==FX_OK);
    assert(fx_zf_module_runtime_bind_execution(&runtime,&arena)==FX_STATE);
    FxServiceExchange exchange={0};
    assert(fx_zf_module_runtime_bind_user_view(&runtime,&context,&api,&exchange,0x1000u,0x2000u,0x3000u)==FX_OK);
    assert(fx_zf_module_runtime_bind_user_view(&runtime,&context,&api,&exchange,0x1000u,0x2000u,0x3000u)==FX_STATE);
    interrupt_entry=1u;
    assert(fx_zf_module_runtime_activate(&runtime,3u)==FX_INTERRUPTED);
    assert(runtime.phase==FX_ZF_MODULE_BOUND && !runtime.initialized && context.continuation[34]);
    assert(fx_zf_module_runtime_activate(&runtime,3u)==FX_OK);
    assert(activate_calls==1u && runtime.phase==FX_ZF_MODULE_ACTIVE && runtime.initialized);
    assert(invoke_calls==0u && gate_calls==3u);
    assert(observed_event_count==0u);
    arena.published=1u;
    assert(fx_zf_module_runtime_invoke(&runtime,4u,&result)==FX_OK);
    assert(invoke_calls==0u && gate_calls==4u && result==0);
    assert(observed_event_count==0u && dispatch_calls==2u && dispatch_saw_request);
    assert(fx_zf_module_runtime_invoke(&runtime,5u,&result)==FX_OK);
    assert(invoke_calls==0u && gate_calls==5u && result==0);
    uint32_t prepared=prepare_calls,acquired=acquires,released=releases,dispatched=dispatch_calls;
    interrupt_entry=1u;
    assert(fx_zf_module_runtime_invoke(&runtime,6u,&result)==FX_INTERRUPTED);
    assert(runtime.phase==FX_ZF_MODULE_ACTIVE && context.continuation[34]);
    assert(acquires==acquired+1u && releases==released && dispatch_calls==dispatched);
    assert(fx_zf_module_runtime_invoke(&runtime,7u,&result)==FX_OK);
    assert(!context.continuation[34] && prepare_calls==prepared+1u);
    assert(acquires==acquired+1u && releases==released+1u && dispatch_calls==dispatched+1u);
    runtime.stage.loader.runtime.phase=FX_ACTIVE;
    runtime.stage.loader.runtime.generation=1u;
    assert(fx_zf_module_runtime_stop(&runtime,8u,0u)==FX_OK);
    assert(invoke_calls==0u && gate_calls==9u && !runtime.initialized &&
           runtime.phase==FX_ZF_MODULE_STOPPED);
    runtime.phase=FX_ZF_MODULE_ACTIVE;runtime.initialized=1u;
    interrupt_entry=1u;
    assert(fx_zf_module_runtime_invoke(&runtime,10u,&result)==FX_INTERRUPTED);
    dispatched=dispatch_calls;released=releases;
    assert(fx_zf_module_runtime_invoke(&runtime,110u,&result)==FX_TIMEOUT);
    assert(runtime.phase==FX_ZF_MODULE_FAILED && releases==released+1u && dispatch_calls==dispatched);
    runtime.phase=FX_ZF_MODULE_ACTIVE;runtime.initialized=1u;runtime.stop_deadline=0u;
    context.continuation[34]=0u;interrupt_entry=1u;
    assert(fx_zf_module_runtime_stop(&runtime,111u,0u)==FX_BUSY);
    assert(context.continuation[34] && runtime.lease.id);
    assert(fx_zf_module_runtime_stop(&runtime,112u,0u)==FX_OK);
    assert(!context.continuation[34] && !runtime.lease.id && runtime.phase==FX_ZF_MODULE_STOPPED);
    return 0;
}
