#include <assert.h>
#include <string.h>
#include "owner_storage.h"
#include "fenestra_native_scene.h"

static FxZfOwnerStorage owner;
static FxZfExecContext *active_context;
static uint32_t entry_calls, acquire_calls, release_calls, stop_calls;
static uint32_t dispatch_calls;
static int entry_result;
static uint32_t events[8], event_count;
static uint32_t stop_busy,action_events;
void fx_platform_pulse(FxPlatformOwner *platform,uint64_t now){(void)platform;(void)now;}
FxResult fx_worker_wait_continue(FxWorkerWait *wait,uint32_t quantum,FxServiceReply *reply,FxServiceOwner *service) {
    (void)wait;(void)quantum;(void)reply;(void)service;return FX_OK;
}
FxResult fx_service_event(FxServiceOwner *services,uint32_t event,uint32_t value) {
    (void)services;assert(event==FX_EVENT_ACTION && value==1u);++action_events;return FX_OK;
}

uint32_t fx_zf_ptp_poll(void) { return 0u; }
void fx_native_scene_poll(FxPlatformOwner *platform) { (void)platform; }

void *fx_memcpy(void *destination,const void *source,size_t bytes) {
    return memcpy(destination,source,bytes);
}
void *fx_memset(void *destination,int value,size_t bytes) {
    return memset(destination,value,bytes);
}

FxResult fx_zf_task_start(FxZfTaskScope *scope,const FxZfTaskConfig *config,
                          const FxZfNativeTaskOps *ops) {
    (void)scope;(void)config;(void)ops;return FX_OK;
}
FxResult fx_native_modules_selected(FxNativeModules *manager,FxManagedModule *selected) {
    (void)manager;(void)selected;return FX_EMPTY;
}
void fx_module_manager_ready(FxModuleManager *manager) { (void)manager; }
void fx_module_manager_tick(FxModuleManager *manager) { (void)manager; }
const FxZfCardNative *fx_zf_card_native_ops(void) { return 0; }
FxResult fx_zf_module_stage(FxZfModuleStage *stage,const FxZfCardNative *native,
    const char *native_path,const char *logical_path,uint32_t bytes,const uint8_t digest[32],
    uint64_t capabilities,uint8_t *file,uint32_t file_capacity,uint8_t *package,
    uint32_t package_capacity,uint8_t *image,uint32_t image_capacity,uint64_t now) {
    (void)stage;(void)native;(void)native_path;(void)logical_path;(void)bytes;(void)digest;
    (void)capabilities;(void)file;(void)file_capacity;(void)package;(void)package_capacity;
    (void)image;(void)image_capacity;(void)now;return FX_OK;
}

FxResult fx_zf_owner_module_reserve_image(FxZfOwnerStorage *storage,uint32_t bytes) {
    (void)storage;(void)bytes;return FX_OK;
}

FxResult fx_zf_owner_module_prepare(FxZfOwnerStorage *storage) {
    static FxServiceModule api;
    static FxServiceExchange exchange;
    assert(storage == &owner && storage->module_runtime.phase == FX_ZF_MODULE_BOUND);
    storage->module_arena.bound = 1u;
    storage->module_image = (uint8_t *)(uintptr_t)0x40000000u;
    storage->module_runtime.stage.loader.image = storage->module_image;
    storage->module_runtime.stage.loader.module.entry_offset = 16u;
    storage->module_exec_context.diagnostics.magic = UINT32_C(0x465a4645);
    storage->module_private_root = 0x1000u;
    storage->module_user_root = 0x2000u;
    storage->module_user_stack = (uint8_t *)(uintptr_t)0x70000000u;
    active_context = &storage->module_exec_context;
    FxResult result = fx_zf_module_runtime_bind_execution(&storage->module_runtime,
                                                           &storage->module_arena);
    if(result == FX_OK)
        result = fx_zf_module_runtime_bind_user_view(&storage->module_runtime,active_context,
            &api,&exchange,storage->module_private_root,storage->module_user_root,
            (uintptr_t)storage->module_user_stack);
    return result;
}

FxNativeModules *fx_zf_owner_modules_manager(void) { return 0; }
FxZfLifecycle *fx_zf_runtime_lifecycle(void) { return &owner.lifecycle; }

FxResult fx_zf_services_prepare(FxZfServices *services,uint64_t now,uint32_t event,uint32_t value) {
    (void)now;(void)value;services->module.event=event;return FX_OK;
}
FxResult fx_zf_services_dispatch_pending(FxZfServices *services) {
    (void)services;++dispatch_calls;return FX_EMPTY;
}
FxResult fx_zf_services_stop(FxZfServices *context,uint32_t panic) {
    (void)context;assert(panic == 0u);++stop_calls;
    if(stop_busy){--stop_busy;return FX_BUSY;}
    return FX_OK;
}
FxResult fx_zf_services_init(FxZfServices *services,uint32_t owner_id,uint32_t generation,
                             uint32_t capabilities) {
    (void)services;(void)owner_id;(void)generation;(void)capabilities;return FX_OK;
}
FxResult fx_zf_services_bind_module(FxZfServices *services,const char *id,const FxModule *module,
                                    void *image,uint32_t bytes) {
    (void)services;(void)id;(void)module;(void)image;(void)bytes;return FX_OK;
}

FxResult fx_exec_live(const FxExecArena *arena) {
    return arena && arena->bound ? FX_OK : FX_CAPABILITY;
}
FxResult fx_loader_activate_mapped(FxLoader *loader,FxExecArena *arena,uint64_t now) {
    (void)now;assert(loader && arena && arena->bound);arena->published=1u;
    loader->runtime.phase=FX_ACTIVE;loader->runtime.generation=1u;return FX_OK;
}
FxResult fx_zf_exec_call(FxZfExecContext *context,uintptr_t root,
                         int (*operation)(FxZfExecContext *)) {
    assert(context == active_context && root == 0x1000u && operation);
    return (FxResult)operation(context);
}
int fx_zf_user_call(uintptr_t entry,const void *api,uintptr_t root,uintptr_t stack,
                    FxZfExecContext *context) {
    (void)root;(void)stack;assert(entry == 0x40000010u && context == active_context);
    const FxServiceModule *module=api;
    assert(module && module->exchange);
    assert(module->event == FX_EVENT_INIT || module->event == FX_EVENT_TICK ||
           module->event == FX_EVENT_STOP);
    events[event_count++]=module->event;++entry_calls;
    return entry_result;
}
FxResult fx_execution_resume(FxRuntime *runtime,uint64_t now) {(void)runtime;(void)now;return FX_OK;}
FxResult fx_execution_idle(FxRuntime *runtime,uint64_t now) {(void)runtime;(void)now;return FX_OK;}
FxResult fx_acquire(FxRuntime *runtime,uint32_t generation,uint64_t now,FxLease *lease) {
    (void)runtime;(void)now;assert(generation == 1u);lease->id=++acquire_calls;
    lease->generation=generation;return FX_OK;
}
FxResult fx_release(FxRuntime *runtime,FxLease lease,uint64_t now) {
    (void)runtime;(void)now;assert(lease.id == release_calls+1u);++release_calls;return FX_OK;
}
FxResult fx_disable(FxRuntime *runtime,uint64_t now,uint64_t timeout) {
    (void)now;assert(timeout == 10000u);runtime->phase=FX_QUIESCING;return FX_OK;
}
FxResult fx_loader_confirm_quiesced(FxLoader *loader,uint32_t generation,uint64_t now) {
    (void)now;assert(generation == 1u && loader->runtime.phase == FX_QUIESCING);
    loader->runtime.phase=FX_DISABLED;return FX_OK;
}
void fx_zf_owner_fault(FxZfOwnerStorage *storage,const FxPlatformOwner *platform,FxResult reason) {
    (void)platform;assert(storage == &owner);atomic_store(&storage->worker.error,(uint32_t)reason);
}

int main(void) {
    memset(&owner,0,sizeof(owner));
    owner.module_runtime.phase=FX_ZF_MODULE_BOUND;
    owner.module_runtime.stage.loader.module.bytes=4096u;
    owner.module_runtime.stage.loader.module.entry_offset=16u;
    owner.module_runtime.services.module.generation=1u;
    atomic_store(&owner.worker.steps,40u);
    atomic_store(&owner.module_runtime.services.action,1u);
    fx_zf_lifecycle_record(&owner.lifecycle,20u);
    fx_zf_lifecycle_record(&owner.lifecycle,25u);

    assert(fx_zf_owner_task_step(&owner)==1);
    assert(owner.module_runtime.phase==FX_ZF_MODULE_ACTIVE && owner.module_runtime.initialized);
    assert(entry_calls==2u && events[0]==FX_EVENT_INIT && events[1]==FX_EVENT_TICK);
    assert(acquire_calls==2u && release_calls==2u);
    assert(action_events==1u && !atomic_load(&owner.module_runtime.services.action));
    assert(atomic_load(&owner.worker.error)==0u);

    fx_zf_lifecycle_record(&owner.lifecycle,8u);
    stop_busy=1u;
    assert(fx_zf_owner_task_step(&owner)==1);
    assert(!owner.module_runtime.initialized && stop_calls==1u);
    assert(fx_zf_owner_task_step(&owner)==0);
    assert(owner.module_runtime.phase==FX_ZF_MODULE_STOPPED && !owner.module_runtime.initialized);
    assert(entry_calls==3u && events[2]==FX_EVENT_STOP);
    assert(acquire_calls==3u && release_calls==3u && stop_calls==2u);
    assert(owner.module_runtime.stage.loader.runtime.phase==FX_DISABLED);
    assert(atomic_load(&owner.worker.error)==0u);
    assert(dispatch_calls==3u);

    /* Nonzero module returns match Z9: release the lease, report the module
     * failure, and never dispatch its possibly incomplete request. */
    owner.module_runtime.phase=FX_ZF_MODULE_ACTIVE;
    owner.module_runtime.initialized=1u;
    owner.module_runtime.stage.loader.runtime.phase=FX_ACTIVE;
    entry_result=22;
    int returned=0;
    assert(fx_zf_module_runtime_invoke(&owner.module_runtime,1u,&returned)==FX_INTEGRITY);
    assert(returned==22 && owner.module_runtime.phase==FX_ZF_MODULE_FAILED);
    assert(acquire_calls==4u && release_calls==4u && dispatch_calls==3u);

    owner.module_runtime.phase=FX_ZF_MODULE_ACTIVE;entry_result=0;
    owner.module_runtime.services.platform.phase=FX_OWNER_FINISHED;
    assert(fx_zf_module_runtime_invoke(&owner.module_runtime,2u,&returned)==FX_OK);
    assert(owner.module_runtime.phase==FX_ZF_MODULE_FINISHED);
    assert(fx_zf_module_runtime_invoke(&owner.module_runtime,3u,&returned)==FX_STATE);
    assert(acquire_calls==5u && release_calls==5u && dispatch_calls==4u);
    owner.module_runtime.stop_deadline=4u;
    assert(fx_zf_module_runtime_stop(&owner.module_runtime,4u,0u)==FX_TIMEOUT);
    return 0;
}
