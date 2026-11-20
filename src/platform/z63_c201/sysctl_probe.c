#include <stdint.h>
/* Entry/return probe for full-system bring-up. These observation points do not
 * allocate an owner, activate modules or change Nikon's event dispatch. */
__attribute__((noinline)) void fx_z63_sysctl_before(uint32_t event,uint32_t sync) {
    __asm__ volatile(""::"r"(event),"r"(sync):"memory");
}
__attribute__((noinline)) void fx_z63_sysctl_after(uint32_t event,uint32_t sync) {
    __asm__ volatile(""::"r"(event),"r"(sync):"memory");
}
typedef struct {
    uint32_t flags,reserved;uintptr_t argument,entry;
    uint32_t priority,reserved2;uint64_t stack_bytes;uintptr_t stack;
    uint32_t target,affinity;
} FxZ63ProbeTask;
_Static_assert(sizeof(FxZ63ProbeTask)==56,"original C2.01 create request");
__attribute__((noinline)) void fx_z63_probe_worker(void *argument) {
    __asm__ volatile(""::"r"(argument):"memory");
}
__attribute__((noinline)) void fx_z63_probe_task_result(int32_t created,int32_t resumed) {
    __asm__ volatile(""::"r"(created),"r"(resumed):"memory");
}
__attribute__((unused)) static void probe_task(void) {
    /* Bounded native automatic-stack experiment: no delete/reuse, no module or
     * buffer publication. Native task/TLS storage remains retained until reboot.
     * This does not qualify automatic allocation for a module workload. */
    FxZ63ProbeTask request={0u,0u,0u,(uintptr_t)fx_z63_probe_worker,
                           100u,0u,4096u,0u,1u,UINT32_MAX};
    int32_t id=((int32_t (*)(const FxZ63ProbeTask *))(uintptr_t)0x4018afb8u)(&request);
    int32_t result=id>0?((int32_t (*)(int32_t))(uintptr_t)0x4018b53cu)(id):INT32_MIN;
    fx_z63_probe_task_result(id,result);
}
extern uint64_t fx_z63_system_original(void *,uint32_t);
uint64_t fx_z63_system_dispatch(void *message,uint32_t sync) {
    uint32_t event=*(const uint32_t *)((const uint8_t *)message+32u);
    fx_z63_sysctl_before(event,sync);
#ifdef FX_Z63_MENU
    extern void fx_z63_menu_install(void);
    extern void fx_z63_menu_lifecycle(uint32_t);
    if(event==8u || event==24u)fx_z63_menu_lifecycle(event);
#endif
    uint64_t result=fx_z63_system_original(message,sync);
    fx_z63_sysctl_after(event,sync);
#ifdef FX_Z63_MENU
    if(event==20u)fx_z63_menu_install();
    if(event==25u)fx_z63_menu_lifecycle(event);
#elif defined(FX_Z63_MODULE_PROBE)
    extern void fx_z63_probe_module_start(void);
    if(event==25u)fx_z63_probe_module_start();
#else
    if(event==20u)probe_task();
#endif
    return result;
}
