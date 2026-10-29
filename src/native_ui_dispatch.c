#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_namespace.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_apps.h"
static uintptr_t current_task(void) {
    uintptr_t context;__asm__ volatile("mrs %0,tpidr_el1":"=r"(context));
    return context?*(const volatile uintptr_t *)context:0u;
}
static int ui_task(FxNativeUiDispatch *d) {
    uintptr_t task=d?atomic_load_explicit(&d->task,memory_order_acquire):0u;
    return task && task==current_task();
}
static int module_page(void) {
    const FxNativePage *page=((const FxNativePage *(*)(void))fx_native_ui_dispatch_profile.current_page)();
    uint32_t id=page?page->identity>>16:0u;
    return fx_native_ui_find_page(id)!=0;
}
void fx_native_ui_capture_task(void) {
    FxNativeUiDispatch *d=fx_native_ui_dispatch_domain();if(d)fx_ui_dispatch_capture(d,current_task());
}
void fx_native_ui_refresh_owner(FxPlatformOwner *owner) {
    FxNativeUiDispatch *d=fx_native_ui_dispatch_domain();if(d)(void)fx_ui_dispatch_request(d,owner);
}
uint32_t fx_native_ui_poll_native(void *flags) {
    uint32_t result=((uint32_t (*)(void *))fx_native_ui_dispatch_profile.poll)(flags);
    FxNativeUiDispatch *d=fx_native_ui_dispatch_domain();
    if(ui_task(d) && fx_native_apps_pending(fx_native_apps_domain()))(void)fx_native_apps_flush_native();
#ifdef FX_NATIVE_TASKS
    if(d && fx_tasks_pending(&d->tasks)) {
        uintptr_t task=current_task();
        if(fx_tasks_capture(&d->tasks,task)==FX_OK)(void)fx_tasks_poll(&d->tasks,task);
    }
#endif
    if(d && atomic_load_explicit(&d->task,memory_order_acquire))
        (void)fx_ui_dispatch_poll(d,current_task(),module_page(),fx_native_ui_flush_owner);
    return result;
}
uint32_t fx_native_ui_wait_native(void) {
    uint32_t result=((uint32_t (*)(void))fx_native_ui_dispatch_profile.next_wait)();
    FxNativeUiDispatch *d=fx_native_ui_dispatch_domain();
    if(ui_task(d) && fx_native_apps_pending(fx_native_apps_domain()) && result>fx_native_ui_dispatch_profile.interval_ms)
        result=fx_native_ui_dispatch_profile.interval_ms;
#ifdef FX_NATIVE_TASKS
    if(d && fx_tasks_pending(&d->tasks)) {
        uintptr_t task=current_task();
        if(fx_tasks_capture(&d->tasks,task)==FX_OK && result>fx_native_ui_dispatch_profile.interval_ms)
            result=fx_native_ui_dispatch_profile.interval_ms;
    }
#endif
    if(d && atomic_load_explicit(&d->task,memory_order_acquire))
        result=fx_ui_dispatch_wait(d,current_task(),module_page(),result,fx_native_ui_dispatch_profile.interval_ms);
    return result;
}
