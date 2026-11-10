/* Discovery and workers retain their published state through guest reboot.
 * No compile-time package name, size or allowlist controls this runtime. */
#include "runtime.h"
#include "discovery.h"
#include "fenestra_platform_native.h"
#include "fenestra_memory.h"
#ifdef FX_Z63_UI
#include "ui.h"
#include "fenestra_native_panic.h"
#endif
#define CAPS FX_SDK_CAPS
struct FxZ63Runtime {
    FxZ63TaskScope task;FxZ63Directory directory;FxModuleInventory inventory;
    FxZ63ModuleWorker **workers;FxZ63Services **services;
    _Atomic uint32_t count,stop,published,error;
#ifdef FX_Z63_UI
    FxNativePanicPage *panic;
#endif
    uint8_t scratch[16385];
};
const uint32_t fx_z63_runtime_layout[]={0x46585236u,1u,offsetof(FxZ63Runtime,services),
    offsetof(FxZ63Runtime,workers),offsetof(FxZ63Runtime,count)};
#ifdef FX_Z63_UI
const uint32_t fx_z63_panic_layout[]={0x46585036u,1u,offsetof(FxZ63Runtime,panic),
    offsetof(FxNativePanicPage,captured),offsetof(FxNativePanicPage,shown),offsetof(FxNativePanicPage,result),
    offsetof(FxNativePanicPage,display)+offsetof(FxNativeDisplay,frames),
    offsetof(FxNativePanicPage,report)+offsetof(FxPanicReport,cpu_faults),
    offsetof(FxNativePanicPage,report)+offsetof(FxPanicReport,esr)};
#endif
__attribute__((noinline)) void fx_z63_probe_module_result(uint32_t stage,uint32_t result,uint64_t detail) {
    __asm__ volatile(""::"r"(stage),"r"(result),"r"(detail):"memory");
}
void fx_native_module_fault(FxPlatformOwner *o,FxResult error) {
    if(!o)return;
    if(error==FX_OK)error=FX_INTEGRITY;
    uint32_t zero=0u;
    (void)atomic_compare_exchange_strong(&o->first_error,&zero,(uint32_t)error);
    atomic_store_explicit(&o->phase,FX_OWNER_FAULT,memory_order_release);
}
static void *allocate(void *context,uint32_t n){(void)context;return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u)(n,0u);}
static void release(void *context,void *p){(void)context;((void (*)(void *))(uintptr_t)0x40d32d40u)(p);}
static void run_work(void *context) {
    FxZ63Runtime *s=context;FxModuleDirectory io;
    const FxPlatform *platform=fx_platform_target();
    FxResult r=fx_z63_directory_init(&s->directory,&io);
    if(r==FX_OK)r=fx_modules_discover(&io,platform->module_identity,CAPS,s->scratch,sizeof(s->scratch),&s->inventory,(FxIndexMemory){0,allocate,release});
    fx_z63_probe_module_result(60u,(uint32_t)r,s->inventory.count);
    if(r!=FX_OK || atomic_load(&s->stop)){atomic_store(&s->error,r?r:FX_STATE);return;}
    uint32_t count=s->inventory.count;
    if(count>65534u){atomic_store(&s->error,FX_BUDGET);fx_z63_probe_module_result(40u,FX_BUDGET,0u);return;}
    if(!count){atomic_store_explicit(&s->published,1u,memory_order_release);return;}
#ifdef FX_Z63_UI
    s->panic=allocate(0,(uint32_t)sizeof(*s->panic));
    if(!s->panic){atomic_store(&s->error,FX_BUDGET);return;}
    fx_native_panic_page_init(s->panic);
#endif
    s->workers=allocate(0,count*(uint32_t)sizeof(*s->workers));
    s->services=allocate(0,count*(uint32_t)sizeof(*s->services));
    if(!s->workers || !s->services){atomic_store(&s->error,FX_BUDGET);fx_z63_probe_module_result(40u,FX_BUDGET,0u);return;}
    memset(s->workers,0,count*sizeof(*s->workers));memset(s->services,0,count*sizeof(*s->services));
    for(uint32_t i=0;i<count && !atomic_load(&s->stop);++i) {
        const FxDiscoveredModule *entry=&s->inventory.entries[i];FxExecLayout layout;
        uint32_t caps=(entry->required|entry->optional)&CAPS;
        r=fx_z63_session_layout_stacks(entry->bytes,entry->image_bytes,entry->stacks,&layout);
        FxZ63Services *services=allocate(0,(uint32_t)sizeof(*services));
        void *worker=allocate(0,fx_z63_worker_bytes());
        void *session=r==FX_OK?allocate(0,layout.bytes+4096u):0;
        if(r==FX_OK && (!services || !worker || !session))r=FX_BUDGET;
        if(r==FX_OK) {
            memset(services,0,sizeof(*services));memset(worker,0,fx_z63_worker_bytes());
            r=fx_z63_services_init(services,i+1u,1u,caps);
#ifdef FX_Z63_UI
            if(r==FX_OK)r=fx_z63_ui_register(&services->platform);
#endif
        }
        if(r==FX_OK) {
            s->services[i]=services;
            r=fx_z63_worker_start(worker,fx_z63_worker_bytes(),session,layout.bytes+4096u,entry,i+1u,1u,caps,
                (FxServiceBackend){services,fx_z63_service_call,fx_z63_service_stop},&s->workers[i]);
        }
        fx_z63_probe_module_result(61u,(uint32_t)r,i);
        if(r!=FX_OK)atomic_store(&s->error,r);
        atomic_store_explicit(&s->count,i+1u,memory_order_release);
        if(atomic_load(&s->stop))fx_z63_worker_shutdown(s->workers[i]);
    }
    atomic_store_explicit(&s->published,1u,memory_order_release);
#ifdef FX_Z63_UI
    /* Inventory/worker publication is complete. Long-lived modules must not
     * freeze manager inspection or outlive the controller's observation. */
    fx_z63_ui_ready();
#endif
    uint32_t reported=0u;
    while(!atomic_load(&s->stop)) {
        uint32_t finished=0,faulted=0,n=atomic_load(&s->count);
        for(uint32_t i=0;i<n;++i){uint32_t phase=fx_z63_worker_phase(s->workers[i]);finished+=(uint32_t)(phase==FX_Z63_WORKER_FINISHED);faulted+=(uint32_t)(!s->workers[i] || phase==FX_Z63_WORKER_PANIC);}
        if(faulted) {
            if(!reported) {
                /* The retained error page describes a stopped activation.
                 * Keep its controller alive to render, but retire every peer
                 * owner through the normal stop/ACK path before another tick. */
                for(uint32_t i=0u;i<n;++i)fx_z63_worker_shutdown(s->workers[i]);
                fx_z63_probe_module_result(63u,faulted,finished);reported=1u;
            }
#ifdef FX_Z63_UI
            if(!s->panic->captured)for(uint32_t i=0u;i<n;++i) {
                if(fx_z63_worker_panic_report(s->workers[i],&s->panic->report)==FX_OK){s->panic->captured=1u;break;}
            }
            if(s->panic->captured)(void)fx_native_panic_poll(s->panic);
#endif
        } else if(finished==n){fx_z63_probe_module_result(63u,0u,finished);break;}
        if(fx_z63_task_delay_ticks(10u)!=FX_OK){fx_z63_probe_module_result(40u,FX_IO,0u);return;}
    }
}
static void run(void *context){run_work(context);
#ifdef FX_Z63_UI
    FxZ63Runtime *s=context;
    if(s->panic && atomic_load(&s->stop)) {
        atomic_store_explicit(&s->panic->render_gate,0u,memory_order_release);
        fx_native_display_detach(&s->panic->display);
    }
    fx_z63_ui_ready();
#endif
}
void fx_z63_probe_module_start(void) {
    FxZ63Runtime *s=allocate(0,(uint32_t)sizeof(*s));void *stack=allocate(0,65536u+16u);
    if(!s || !stack){fx_z63_probe_module_result(2u,FX_BUDGET,0u);return;}
    memset(s,0,sizeof(*s));stack=(void *)(((uintptr_t)stack+15u)&~(uintptr_t)15u);
    fx_z63_menu_set_runtime(s);
    FxResult r=fx_z63_task_start(&s->task,run,s,stack,65536u);
    if(r!=FX_OK)atomic_store(&s->error,r);
    fx_z63_probe_module_result(4u,(uint32_t)r,(uint32_t)s->task.id);
}
void fx_z63_runtime_stop(FxZ63Runtime *s){if(!s)return;atomic_store(&s->stop,1u);for(uint32_t i=0;i<atomic_load(&s->count);++i)fx_z63_worker_shutdown(s->workers[i]);}
uint32_t fx_z63_runtime_state(const FxZ63Runtime *s) {
    if(!s)return 0u;
    if(atomic_load(&s->error))return 2u;
    if(!atomic_load_explicit(&s->published,memory_order_acquire))return 0u;
    uint32_t count=atomic_load(&s->count),quiesced=0u,pending=0u;
    for(uint32_t i=0u;i<count;++i) {
        uint32_t phase=fx_z63_worker_phase(s->workers[i]);
        if(phase==FX_Z63_WORKER_PANIC || !s->workers[i])return 2u;
        quiesced+=(uint32_t)(phase==FX_Z63_WORKER_QUIESCED);
        pending+=(uint32_t)(phase==FX_Z63_WORKER_LOADING || phase==FX_Z63_WORKER_EMPTY || phase==FX_Z63_WORKER_STOPPING);
    }
    if(atomic_load(&s->stop) && quiesced==count)return 3u;
    return pending?0u:1u;
}
uint32_t fx_z63_runtime_count(const FxZ63Runtime *s){return s?atomic_load_explicit(&s->count,memory_order_acquire):0u;}
FxZ63ModuleWorker *fx_z63_runtime_worker(const FxZ63Runtime *s,uint32_t i){return s && i<fx_z63_runtime_count(s)?s->workers[i]:0;}
FxPlatformOwner *fx_z63_runtime_owner(const FxZ63Runtime *s,uint32_t i){return s && i<fx_z63_runtime_count(s) && s->services[i]?&s->services[i]->platform:0;}
