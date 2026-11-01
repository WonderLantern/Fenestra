#include "services.h"
#include "fenestra_platform_native.h"
#include "fenestra_native_ui_callbacks.h"
FxResult fx_z8_services_init(FxZ8Services *s, uint32_t owner, uint32_t generation, uint32_t caps) {
    if (!s)
        return FX_ARGUMENT;
    return fx_platform_init(&s->platform, fx_platform_target(), owner, generation, caps);
}
FxResult fx_z8_service_call(void *context, const FxServiceRequest *q, FxServiceReply *a) {
    FxZ8Services *s = context;
    if (!s)
        return FX_ARGUMENT;
    uint64_t before=fx_platform_ui_stamp(&s->platform);
    FxResult result=fx_platform_dispatch(&s->platform, q, a);
    if(fx_platform_ui_changed(&s->platform,before))
        fx_native_ui_refresh_owner(&s->platform);
    return result;
}
FxResult fx_z8_service_stop(void *context, uint32_t panic) {
    FxZ8Services *s = context;
    if (!s)
        return FX_ARGUMENT;
    return fx_platform_stop(&s->platform, panic);
}
