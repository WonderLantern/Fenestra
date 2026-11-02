#if !defined(FX_ZF_EXPERIMENTAL_APPS_FLUSH) || FX_ZF_EXPERIMENTAL_APPS_FLUSH != 1
#error "Zf Apps navigation requires the checked experimental GUI profile"
#endif
#include "fenestra_native_apps.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_context.h"
#include "fx_zf_dispatch_profile.h"

/* Original captured GUI task only. Prepare copied work under own-Zf drawing
 * exclusion, release it before navigation (which may synchronously call native
 * page callbacks). A task ID alone grants no authority to publish GUI state. */
FxResult fx_native_apps_flush_native(void) {
    FxNativeApps *apps=fx_native_apps_domain();
    if(!fx_native_apps_pending(apps))return FX_EMPTY;
    FxNativeUiDispatch *domain=fx_native_ui_dispatch_domain();
    uintptr_t task=domain?atomic_load_explicit(&domain->task,memory_order_acquire):0u;
    if(!task)return FX_STATE;
    uintptr_t context=(uintptr_t)fx_native_context(FX_CONTEXT_TPIDR_EL1,0,0,0,0,0,0,0);
    if(!context || *(const volatile uintptr_t *)context!=task)return FX_STATE;
    void *exclusion=(void *)(uintptr_t)FX_ZF_GUI_OBJECT;
    ((void (*)(void *))(uintptr_t)FX_ZF_GUI_ENTER)(exclusion);
    const FxNativePage *page=((const FxNativePage *(*)(void))(uintptr_t)FX_ZF_DISPATCH_CURRENT_PAGE)();
    uint32_t destination=0u;
    FxResult result=fx_native_apps_prepare_navigation(apps,page,&destination);
    ((void (*)(void *))(uintptr_t)FX_ZF_GUI_LEAVE)(exclusion);
    if(result==FX_OK && destination)
        ((void (*)(uint32_t))(uintptr_t)FX_ZF_GUI_NAVIGATE)(destination);
    return result;
}
