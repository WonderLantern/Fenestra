#include "owner_storage.h"
#include "fenestra_native_linux.h"
#include "fenestra_memory.h"
#include "fx_zf_card_profile.h"
#define FN(a,t) ((t)(uintptr_t)(a))
static const FxStartupFileProfile profile={
    FN(FX_ZF_CARD_OPEN,int32_t (*)(const char *,uint32_t,uint32_t)),
    FN(FX_ZF_CARD_READ,uint32_t (*)(int32_t,void *,uint32_t,uint32_t *)),
    FN(FX_ZF_CARD_WRITE,uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *)),
    FN(FX_ZF_CARD_CLOSE,uint32_t (*)(int32_t)),
    FN(FX_ZF_CARD_REMOVE,uint32_t (*)(const char *)),
    {"A:\\FENESTRA\\boot-zf.flag","A:\\FENESTRA\\startup-zf.0","A:\\FENESTRA\\startup-zf.1"},
    UINT32_C(0x80000010)
};
static FxResult activate(void *context) {
    FxZfOwnerStorage *s=context;
    if(!s->modules_manager)return FX_STATE;
    FxResult r=fx_module_manager_freeze(&s->modules_manager->manager);
    if(r!=FX_OK)return r;
    s->panic=FN(0x40d31898u,void *(*)(uint32_t,uint32_t))((uint32_t)sizeof(*s->panic),0u);
    if(!s->panic)return FX_BUDGET;
    fx_native_panic_page_init(s->panic);
    (void)fx_account_acquire(&s->accounting.loader,sizeof(*s->panic),1u,1u);
    return fx_zf_ptp_set_activated(1);
}
static FxResult health(void *context) {
    FxZfOwnerStorage *s=context;FxZfModuleController *c=&s->module_controller;
    if(c->error)return c->error;
    if(atomic_load_explicit(&s->first_fault,memory_order_acquire))return FX_INTEGRITY;
    /* Controller publishes ACTIVE only after every enabled package has
     * released source scratch and reached ACTIVE/FINISHED. */
    return c->phase==3u?FX_OK:FX_BUSY;
}
static FxResult stop(void *context) {
    FxZfOwnerStorage *s=context;
    fx_linux_revoke();fx_rtos_revoke();
    FxResult r=fx_zf_modules_step(s,1u);
    if(r!=FX_OK)return r;
    for(uint32_t i=0;i<atomic_load_explicit(&s->module_controller.count,memory_order_acquire);++i)
        if(s->module_controller.workers[i] &&
           fx_zf_worker_phase(s->module_controller.workers[i])==FX_ZF_WORKER_PANIC)
            return FX_INTEGRITY;
    if(atomic_load_explicit(&s->first_fault,memory_order_acquire))return FX_INTEGRITY;
    return fx_linux_quiesced() && fx_rtos_quiesced()?FX_OK:FX_BUSY;
}
static uint64_t token(void *context) {
    (void)context;uint64_t value;__asm__ volatile("mrs %0,cntpct_el0":"=r"(value));return value;
}
FxResult fx_zf_startup_prepare(FxZfOwnerStorage *s) {
    if(!s || atomic_load(&s->startup_ready))return FX_STATE;
    fx_startup_service_init(&s->startup,UINT32_C(0x005a0301),
        (FxStartupBackend){s,activate,health,stop,token});
    s->startup_files=(FxStartupFiles){.profile=&profile,.shutdown=&s->startup.shutdown,
        .cleanup_window=&s->startup.cleanup_window,
        .observer=fx_capacity_file_observer(&s->capacity_registry)};
    FxResult r=fx_startup_files(&s->startup.io,&s->startup_files);
    if(r==FX_OK)atomic_store_explicit(&s->startup_ready,1u,memory_order_release);
    return r;
}
void fx_zf_startup_tick(FxZfOwnerStorage *s,uint32_t shutdown) {
    if(!s || !atomic_load_explicit(&s->startup_ready,memory_order_acquire))return;
    uint32_t phase=atomic_load(&s->startup.phase),desired=atomic_load(&s->startup.desired),error=atomic_load(&s->startup.error);
    if(shutdown)atomic_store_explicit(&s->startup.shutdown,1u,memory_order_release);
    fx_startup_service_tick(&s->startup);
    if(atomic_load_explicit(&s->startup.phase,memory_order_acquire)==FX_STARTUP_FAULT)
        fx_zf_owner_fault(s,0,(FxResult)atomic_load(&s->startup.error));
    if(!atomic_load(&s->startup.shutdown) && (phase!=atomic_load(&s->startup.phase) ||
       desired!=atomic_load(&s->startup.desired) || error!=atomic_load(&s->startup.error))) {
        extern void fx_zf_menu_refresh_core(void);
        fx_zf_menu_refresh_core();
    }
}
void fx_zf_startup_before_shutdown(void) {
    FxZfLifecycle *life=fx_zf_runtime_lifecycle();if(!life)return;
    FxZfOwnerStorage *s=(void *)((uint8_t *)life - offsetof(FxZfOwnerStorage,lifecycle));
    if(!atomic_load_explicit(&s->startup_ready,memory_order_acquire))return;
    atomic_store(&s->startup.cleanup_window,1u);
    atomic_store_explicit(&s->startup.shutdown,1u,memory_order_release);
    for(uint32_t i=0;i<15u && !atomic_load_explicit(&s->startup.shutdown_done,memory_order_acquire);++i)
        (void)FN(0x40187b00u,int32_t (*)(uint32_t))(33u);
    atomic_store(&s->startup.cleanup_window,0u);
}
void fx_zf_panic_flush(FxZfOwnerStorage *s) {
    if(!s || !s->panic || !atomic_load_explicit(&s->first_fault,memory_order_acquire))return;
    FxNativePanicPage *p=s->panic;
    if(!p->captured) {
        uint64_t first=atomic_load_explicit(&s->first_fault,memory_order_acquire);
        p->report=(FxPanicReport){.module="Fenestra",.error=(FxResult)(uint32_t)first,.module_result=INT32_MIN};
        if(!(first>>32) && s->module_controller.error==(FxResult)(uint32_t)first)
            p->report.module=fx_zf_modules_fault_stage(&s->module_controller);
        for(uint32_t i=0;i<atomic_load_explicit(&s->module_controller.count,memory_order_acquire);++i)
            if(s->module_controller.services[i] && s->module_controller.workers[i] &&
               s->module_controller.services[i]->platform.owner==(uint32_t)(first>>32)) {
                if(fx_zf_worker_panic_report(s->module_controller.workers[i],&p->report)!=FX_OK)return;
                break;
            }
        p->captured=1u;
    }
    (void)fx_native_panic_poll(p);
}
