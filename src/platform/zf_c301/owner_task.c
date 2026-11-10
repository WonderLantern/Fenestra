#if !defined(FX_ZF_EXPERIMENTAL_OWNER_TASK) || FX_ZF_EXPERIMENTAL_OWNER_TASK != 1
#error "Zf owner consumer task is experimental, not a module executor"
#endif
#include "owner_storage.h"
#include "module_execution.h"
#include "fenestra_memory.h"
#include "fenestra_native_scene.h"
#include "fenestra_resources.h"
#include "ptp_admission.h"
#if defined(FX_ZF_EXPERIMENTAL_MENU_NATIVE) && FX_ZF_EXPERIMENTAL_MENU_NATIVE == 1
#include "menu_model.h"
#endif
#if defined(FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR) && FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR == 1
#if !defined(FX_ZF_MODULE_WORKERS) || FX_ZF_MODULE_WORKERS != 1
static uint64_t clock_ms(void) {
#if defined(__aarch64__)
    uint64_t count,freq;
    __asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0":"=r"(count),"=r"(freq));
    if(!freq || freq>UINT32_MAX)return UINT64_MAX;
    return count/freq*1000u+count%freq*1000u/freq;
#else
    return 0u;
#endif
}
static FxResult module_executor_step(FxZfOwnerStorage *owner,uint32_t stop,int *module_result) {
    if(!owner || !module_result || (!stop && atomic_load_explicit(&owner->worker.steps,memory_order_acquire)<40u))
        return FX_STATE;
    FxZfModuleRuntime *runtime=&owner->module_runtime;
    /* The card bit is the admission trigger for the first load only.  Once
     * the package is validated and bound, later worker ticks must be allowed
     * to finish private-arena publication even after that one-shot bit is
     * consumed by the lifecycle latch. */
    FxNativeModules *manager=fx_zf_owner_modules_manager();
    if(runtime->phase==FX_ZF_MODULE_EMPTY && !stop &&
       (!manager || !manager->manager.activated))return FX_STATE;
    uint64_t now=clock_ms();
    if(now==UINT64_MAX)return FX_CAPABILITY;
    if(stop)return fx_zf_module_runtime_stop(runtime,now,0u);
    FxResult result=FX_OK;
    if(runtime->phase==FX_ZF_MODULE_EMPTY && !stop) {
        FxManagedModule selected={0};
        if(!manager)return FX_STATE;
        result=fx_native_modules_selected(manager,&selected);
        if(result==FX_EMPTY) {
            fx_module_manager_ready(&manager->manager);
            return FX_STATE;
        }
        if(result!=FX_OK)return result;
        if(!selected.bytes || selected.bytes>FX_NATIVE_MODULE_PACKAGE_LIMIT ||
           selected.image_bytes>FX_MODULE_MEMORY_LIMIT)return FX_BOUNDS;
        char native_path[64]="A:\\FENESTRA\\";
        char logical_path[48]="FENESTRA/";
        uint32_t n=0u;while(n<sizeof(selected.file) && selected.file[n])++n;
        if(n<5u || n>=sizeof(selected.file) || n+12u>=sizeof(native_path) || n+9u>=sizeof(logical_path))return FX_ARGUMENT;
        memcpy(native_path+12u,selected.file,n+1u);memcpy(logical_path+9u,selected.file,n+1u);
        result=fx_zf_owner_module_reserve_image(owner,selected.image_bytes);
        if(result!=FX_OK)return result;
        if(!owner->module_file) {
            owner->module_file=((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d31898u)(selected.bytes+1u,0u);
            if(!owner->module_file)return FX_BUDGET;
            owner->module_file_capacity=selected.bytes+1u;
        }
        if(!owner->module_package) {
            owner->module_package=((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d31898u)(selected.bytes,0u);
            if(!owner->module_package)return FX_BUDGET;
            owner->module_package_capacity=selected.bytes;
        }
        if(owner->module_file_capacity<selected.bytes+1u ||
           owner->module_package_capacity<selected.bytes)return FX_BOUNDS;
        memcpy(owner->module_id,selected.id,sizeof(owner->module_id));
        result=fx_zf_module_runtime_prepare(runtime,fx_zf_card_native_ops(),
            native_path,logical_path,selected.bytes,selected.digest,FX_SDK_CAPS,
            owner->module_file,owner->module_file_capacity,owner->module_package,
            owner->module_package_capacity,owner->module_image_raw,selected.image_bytes,now);
    }
    if(result==FX_OK && runtime->phase==FX_ZF_MODULE_PREPARED) {
        FxModuleStacks stacks=runtime->stage.loader.module.stacks;
        if(!fx_module_stacks_valid(stacks) || stacks.native_bytes>sizeof(owner->worker.stack))
            return FX_CAPABILITY;
        result=fx_zf_module_runtime_bind(runtime,owner->module_id,1u,1u,FX_SDK_CAPS);
    }
    /* A validated image is not executable until the owner has published a
     * real private arena.  Keep the module in BOUND while that backend is
     * absent; calling activate here would turn an expected capability gap
     * into a spurious owner fault on every worker tick. */
    if(result==FX_OK && runtime->phase==FX_ZF_MODULE_BOUND) {
        if(!runtime->execution) {
            result=fx_zf_owner_module_prepare(owner);
            if(result!=FX_OK)return result;
        }
        now=clock_ms();
        if(now==UINT64_MAX)return FX_CAPABILITY;
        result=fx_zf_module_runtime_activate(runtime,now);
        if(result==FX_OK && manager)fx_module_manager_ready(&manager->manager);
    }
    if(result==FX_OK && runtime->phase==FX_ZF_MODULE_ACTIVE && !stop) {
        FxZfServices *services=&runtime->services;
        uint32_t pending=runtime->exec_context && runtime->exec_context->continuation[34];
        services->pacing=FX_WORKER_POLL;
        if(!pending && services->wait.armed) {
            if(atomic_load_explicit(&services->platform.tick_subscriptions,memory_order_acquire) &&
               now>=services->tick_due) {
                fx_platform_pulse(&services->platform,now);
                services->tick_due=now+services->wait.period_ms;
            }
            (void)fx_worker_wait_continue(&services->wait,1u,&services->exchange.reply,&services->protocol);
            return FX_BUSY;
        }
        if(pending || services->wait.resumed || now>=services->next_due ||
           atomic_load_explicit(&services->action,memory_order_acquire)) {
            if(!pending) {
                services->wait.resumed=0u;
                uint32_t action=atomic_load_explicit(&services->action,memory_order_acquire);
                if(action && fx_service_event(&services->protocol,FX_EVENT_ACTION,action)==FX_OK)
                    (void)atomic_compare_exchange_strong(&services->action,&action,0u);
                if(now>=services->tick_due) {
                    fx_platform_pulse(&services->platform,now);
                    services->tick_due=now+services->wait.period_ms;
                }
            }
            result=fx_zf_module_runtime_invoke(runtime,now,module_result);
            if(result==FX_OK)services->next_due=services->pacing==FX_WORKER_POLL?
                now+services->wait.period_ms:now;
        }
    }
    return result;
}
#endif
#endif
int fx_zf_owner_task_step(FxZfOwnerStorage *owner) {
    if(!owner)return 0;
    uint32_t word=fx_zf_lifecycle_take(&owner->lifecycle);
    atomic_store_explicit(&owner->worker.last_word,word,memory_order_release);
    atomic_fetch_add_explicit(&owner->worker.steps,1u,memory_order_relaxed);
    if(word&FX_ZF_LIFE_PENDING)atomic_fetch_add_explicit(&owner->worker.notices,1u,memory_order_relaxed);
    FxNativeModules *manager=fx_zf_owner_modules_manager();
    if(manager)fx_module_manager_tick(&manager->manager);
#if defined(FX_ZF_EXPERIMENTAL_MENU_NATIVE) && FX_ZF_EXPERIMENTAL_MENU_NATIVE == 1
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
    if((word&FX_ZF_LIFE_CARD_PENDING) && (word&FX_ZF_LIFE_CARD))
        atomic_store_explicit(&owner->startup.inspect_requested,1u,memory_order_release);
    fx_zf_startup_tick(owner,(word&FX_ZF_LIFE_STOP)!=0u);
#else
    FxZfMenu *menu=(void *)atomic_load_explicit(&owner->menu,memory_order_acquire);
    if(menu && atomic_load_explicit(&menu->activation_requested,memory_order_acquire) && manager &&
       !(word&FX_ZF_LIFE_STOP)) {
        FxResult frozen=fx_module_manager_freeze(&manager->manager);
        if(frozen==FX_OK) {
            (void)fx_zf_ptp_set_activated(1);
            atomic_store_explicit(&menu->activation_requested,0u,memory_order_release);
        }
    }
#endif
#endif
#if defined(FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR) && FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR == 1
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
    FxResult module=fx_zf_modules_step(owner,(word&FX_ZF_LIFE_STOP)!=0u);
#else
    int module_result=0;
    FxResult module=module_executor_step(owner,(word&FX_ZF_LIFE_STOP)!=0u,&module_result);
#endif
    if(module!=FX_OK && module!=FX_BUSY && module!=FX_STATE && module!=FX_EMPTY && module!=FX_INTERRUPTED) {
        atomic_store_explicit(&owner->worker.error,(uint32_t)module,memory_order_release);
        fx_zf_owner_fault(owner,0,module);
    }
#endif
    /* Polling is owner-task work; the adapter returns CLOSED while the
     * endpoint remains disabled, preserving the stock USB path. */
    (void)fx_zf_ptp_poll();
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
    /* Match Z9's loader: retained panic display is polled by its owner,
     * independently of Apps navigation or capture of a menu UI task. */
    fx_zf_panic_flush(owner);
#endif
    /* Scene submission is owned by the module worker; rendering is committed
     * by the original UI task through its registered Zf dispatcher. */
#if defined(FX_ZF_EXPERIMENTAL_MODULE_STAGE_PROBE) && FX_ZF_EXPERIMENTAL_MODULE_STAGE_PROBE == 1
    if(owner->module_runtime.services.platform.platform)
        fx_native_scene_poll(&owner->module_runtime.services.platform);
#elif defined(FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR) && FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR == 1 && !defined(FX_ZF_MODULE_WORKERS)
    if(owner->module_runtime.services.platform.platform)
        fx_native_scene_poll(&owner->module_runtime.services.platform);
#endif
    if(!(word&FX_ZF_LIFE_STOP))return 1;
#if defined(FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR) && FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR == 1
    if(module==FX_BUSY)return 1;
#endif
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
    if(atomic_load_explicit(&owner->startup_ready,memory_order_acquire) &&
       !atomic_load_explicit(&owner->startup.shutdown_done,memory_order_acquire) &&
       atomic_load_explicit(&owner->startup.phase,memory_order_acquire)!=FX_STARTUP_FAULT)return 1;
#endif
    return 0;
}
static void worker(void *argument) {
    FxZfOwnerStorage *owner=argument;
    uintptr_t task=0;
#if defined(__aarch64__)
    uintptr_t context;__asm__ volatile("mrs %0,tpidr_el1":"=r"(context));
    if(context)task=*(const volatile uintptr_t *)context;
#endif
    atomic_store_explicit(&owner->worker.task_pointer,task,memory_order_release);
    atomic_store_explicit(&owner->worker.entered,1u,memory_order_release);
    while(fx_zf_owner_task_step(owner)) {
        uint32_t ticks=owner->worker.delay_ticks;
#if defined(FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR) && FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR == 1
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
        if(owner->module_controller.phase)ticks=10u;
#else
        FxZfModuleRuntime *runtime=&owner->module_runtime;
        if(runtime->phase==FX_ZF_MODULE_ACTIVE || runtime->stop_deadline ||
           (runtime->exec_context && runtime->exec_context->continuation[34])) {
            if(runtime->services.pacing==FX_WORKER_CONTINUE && !runtime->services.wait.armed &&
               !(atomic_load_explicit(&owner->lifecycle.word,memory_order_acquire)&FX_ZF_LIFE_STOP))continue;
            ticks=1u;
        }
#endif
#endif
        if(owner->worker.delay(ticks)) {
            atomic_store_explicit(&owner->worker.error,FX_IO,memory_order_release);
            fx_zf_lifecycle_record(&owner->lifecycle,8u);
        }
    }
    atomic_store_explicit(&owner->worker.finished,1u,memory_order_release);
}
FxResult fx_zf_owner_task_start(FxZfOwnerStorage *owner,const FxZfNativeTaskOps *ops,
                              int32_t (*delay)(uint32_t),uint32_t ticks,uint32_t priority,uint32_t mode) {
    if(!owner || !ops || !delay || !ticks)return FX_ARGUMENT;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong_explicit(&owner->worker.start_claimed,&zero,1u,
            memory_order_acq_rel,memory_order_acquire))return FX_BUSY;
    owner->worker.delay=delay;owner->worker.delay_ticks=ticks;
    const FxZfTaskConfig config={worker,owner,owner->worker.stack,sizeof(owner->worker.stack),priority,mode};
    FxResult result=fx_zf_task_start(&owner->worker.scope,&config,ops);
    if(result!=FX_OK)atomic_store_explicit(&owner->worker.error,(uint32_t)result,memory_order_release);
    /* On create/start failure, retain storage and task ID. Never retry or free
     * a possibly scheduled native entry. No clean-shutdown claim is made. */
    return result;
}
