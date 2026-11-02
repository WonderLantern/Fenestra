#if !defined(FX_ZF_EXPERIMENTAL_OWNER_NATIVE) || FX_ZF_EXPERIMENTAL_OWNER_NATIVE != 1
#error "Zf owner publication backend is experimental; no production hook"
#endif
#include "owner_publish.h"
#include "fenestra_memory.h"
static void *allocate(void *,uint32_t);
static void release(void *,void *);
typedef struct {void *raw;} FxZfOwnerAllocation;
static void *allocate_owner(void *,uint32_t);
static void release_owner(void *,void *);
static FxNativePage *setup_page(void) {
    uintptr_t base=((const volatile uintptr_t *)(uintptr_t)UINT64_C(0xa03401b88))[2];
    if(base<UINT64_C(0xa00000000) || base>UINT64_C(0xa40000000)-672u*96u-96u)return 0;
    return (void *)(base+672u*96u);
}
static int owned_range(uintptr_t address) {
    const volatile uint64_t *p=(const void *)(uintptr_t)UINT64_C(0xa033504f0);
    for(unsigned i=0;i<2;i++,p+=4) {
        uint64_t handle=p[0],bytes=p[1],start=p[2];
        if(start<UINT64_C(0xa00000000) || start>UINT64_C(0xa40000000)-16u ||
           handle!=start+16u || bytes<sizeof(FxZfOwnerStorage) || bytes>UINT64_C(0xa40000000)-start)continue;
        if(address>=start && address<=start+bytes-sizeof(FxZfOwnerStorage))return 1;
    }
    return 0;
}
FxZfLifecycle *fx_zf_runtime_lifecycle(void) {
    FxNativePage *page=setup_page();if(!page)return 0;
    uintptr_t at=atomic_load_explicit((_Atomic uintptr_t *)&page->descriptor,memory_order_acquire);
    if(at%_Alignof(FxZfOwnerStorage) || !owned_range(at))return 0;
    FxZfOwnerStorage *s=(void *)at;
    if(s->magic!=UINT32_C(0x46585a46) || s->version!=12u || s->page!=(uintptr_t)page ||
       s->original_descriptor!=UINT64_C(0xa028cd3b0) ||
       memcmp(&s->stock,(const void *)(uintptr_t)UINT64_C(0xa028cd3b0),64u))return 0;
    return &s->lifecycle;
}
static FxZfOwnerStorage *owner(void) {
    FxZfLifecycle *life=fx_zf_runtime_lifecycle();
    return life?(void *)((uint8_t *)life - offsetof(FxZfOwnerStorage,lifecycle)):0;
}
FxResult fx_zf_owner_module_action(FxPlatformOwner *platform,uint32_t action) {
#if defined(FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR) && FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR == 1
    FxZfOwnerStorage *s=owner();
    if(!platform || !action)return FX_ARGUMENT;
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
    if(!s || (atomic_load_explicit(&s->lifecycle.word,memory_order_acquire)&FX_ZF_LIFE_STOP))return FX_STATE;
    return fx_zf_modules_action(&s->module_controller,platform,action);
#else
    if(!s || platform!=&s->module_runtime.services.platform ||
       atomic_load_explicit(&platform->phase,memory_order_acquire)!=FX_OWNER_RUNNING ||
       (atomic_load_explicit(&s->lifecycle.word,memory_order_acquire)&FX_ZF_LIFE_STOP))return FX_STATE;
    FxZfServices *services=&s->module_runtime.services;uint32_t zero=0u;
    if(!atomic_compare_exchange_strong_explicit(&services->action,&zero,action,
        memory_order_release,memory_order_relaxed))return FX_BUSY;
    fx_worker_wait_signal(&services->wait);return FX_OK;
#endif
#else
    (void)platform;(void)action;return FX_CAPABILITY;
#endif
}
void fx_native_module_fault(FxPlatformOwner *module,FxResult error){fx_zf_owner_fault(owner(),module,error);}
FxPtp *fx_zf_ptp_owner(void){FxZfOwnerStorage *s=owner();return s?&s->ptp:0;}
FxZfPtpAuthorization *fx_zf_ptp_authorization(void){FxZfOwnerStorage *s=owner();return s?&s->ptp_authorization:0;}
FxZfPtpAdmission *fx_zf_ptp_admission(void){FxZfOwnerStorage *s=owner();return s?&s->ptp_admission:0;}
FxRtos *fx_zf_owner_rtos(void){FxZfOwnerStorage *s=owner();return s?&s->rtos:0;}
FxLinuxBridge *fx_zf_owner_linux(void){FxZfOwnerStorage *s=owner();return s?&s->linux:0;}
FxZfLoadedModules *fx_zf_owner_modules(void){FxZfOwnerStorage *s=owner();return s?&s->modules:0;}
struct FxNativeModules *fx_zf_owner_modules_manager(void){FxZfOwnerStorage *s=owner();return s?s->modules_manager:0;}
FxZfAppsDomain *fx_zf_owner_apps_domain(void){FxZfOwnerStorage *s=owner();return s?&s->apps_domain:0;}
FxHookRegistry *fx_native_hooks(void){FxZfOwnerStorage *s=owner();return s?&s->hooks:0;}
FxNativeMappingRegistry *fx_native_mappings(void){FxZfOwnerStorage *s=owner();return s?&s->mappings:0;}
FxNativeUiDispatch *fx_native_ui_dispatch_domain(void){FxZfOwnerStorage *s=owner();return s?&s->ui_dispatch:0;}
FxAccountState *fx_native_shared_accounts(void){
    FxZfOwnerStorage *s=owner();
    return s && atomic_load_explicit(&s->accounting.ready,memory_order_acquire)?&s->accounting.state:0;
}
FxCapacityRegistry *fx_native_capacity_registry(void){
    FxZfOwnerStorage *s=owner();
    return s && atomic_load_explicit(&s->capacity_registry.ready,memory_order_acquire)?&s->capacity_registry:0;
}
FxModuleBus *fx_native_module_bus(void){
    FxZfOwnerStorage *s=owner();if(!s)return 0;
    if(fx_module_bus_init(&s->bus,(FxIndexMemory){0,allocate,release})!=FX_OK)return 0;
    return &s->bus;
}
static void *allocate(void *context,uint32_t bytes) {
    (void)context;return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d31898u)(bytes,0u);
}
static void release(void *context,void *memory) {
    (void)context;((void (*)(void *))(uintptr_t)0x40d318d8u)(memory);
}
static void *allocate_owner(void *context,uint32_t bytes) {
    FxZfOwnerAllocation *allocation=context;
    if(!allocation || bytes>UINT32_MAX-4095u)return 0;
    void *raw=((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d31898u)(bytes+4095u,0u);
    if(!raw)return 0;
    uintptr_t aligned=((uintptr_t)raw+4095u)&~(uintptr_t)4095u;
    allocation->raw=raw;
    return (void *)aligned;
}
static void release_owner(void *context,void *memory) {
    FxZfOwnerAllocation *allocation=context;
    (void)memory;
    if(allocation && allocation->raw)
        ((void (*)(void *))(uintptr_t)0x40d318d8u)(allocation->raw);
}
FxResult fx_zf_owner_bootstrap(void) {
    uintptr_t context;__asm__ volatile("mrs %0,tpidr_el1":"=r"(context));
    if(!context || ((uint32_t (*)(void))(uintptr_t)0x40183490u)())return FX_STATE;
    if(fx_zf_runtime_lifecycle())return FX_OK;
    FxNativePage *page=setup_page();if(!page)return FX_STATE;
    FxZfOwnerAllocation allocation={0};
    const FxZfOwnerMemory memory={&allocation,allocate_owner,release_owner};
    FxResult result=fx_zf_owner_publish(page,(const void *)(uintptr_t)UINT64_C(0xa028cd3b0),&memory);
    return result==FX_STALE && fx_zf_runtime_lifecycle()?FX_OK:result;
}
FxResult fx_zf_owner_start_native_task(uint32_t priority,uint32_t mode,uint32_t ticks) {
    FxZfLifecycle *life=fx_zf_runtime_lifecycle();if(!life)return FX_STATE;
    FxZfOwnerStorage *owner=(void *)((uint8_t *)life - offsetof(FxZfOwnerStorage,lifecycle));
    const FxZfNativeTaskOps ops={
        (int32_t (*)(const FxZfNativeTaskRequest *))(uintptr_t)0x40185064u,
        (int32_t (*)(int32_t))(uintptr_t)0x401855e0u};
    return fx_zf_owner_task_start(owner,&ops,(int32_t (*)(uint32_t))(uintptr_t)0x40187b00u,
                                  ticks,priority,mode);
}
