#if !defined(FX_ZF_EXPERIMENTAL_DISPATCH_PROFILE) || FX_ZF_EXPERIMENTAL_DISPATCH_PROFILE != 1
#error "Zf dispatch profile requires the hash-checked experimental build"
#endif
#include "fenestra_native_ui_dispatch.h"
#include "fx_zf_dispatch_profile.h"
/* Original helper/call-site ABI only. No task owner is captured and no hook
 * is installed here. 16 ms is the shared Fenestra work-queue policy. */
const FxNativeUiDispatchProfile fx_native_ui_dispatch_profile={
    FX_ZF_DISPATCH_CURRENT_PAGE,FX_ZF_DISPATCH_POLL,FX_ZF_DISPATCH_WAIT,16u,0u,
    FX_ZF_DISPATCH_EVENT_GROUP,FX_ZF_DISPATCH_EVENT_SET,FX_ZF_DISPATCH_WAKE_MASK,0u};
