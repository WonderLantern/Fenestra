#include "fenestra_native_sdk.h"
static const FxNativeBootstrap *bootstrap;
static uint64_t provider;
static _Atomic uint64_t calls;
static FxResult count(void *context, const FxProviderClient *caller, uint64_t operation, const void *data,
                      uint32_t bytes, FxServiceReply *reply) {
    (void)context;
    (void)data;
    if (operation || bytes)
        return FX_ARGUMENT;
    reply->value[0] = atomic_fetch_add_explicit(&calls, 1u, memory_order_relaxed) + 1u;
    reply->value[1] = caller->owner;
    return FX_OK;
}
static FxResult shutdown(void *context, uint32_t event) {
    (void)context;
    (void)event;
    return bootstrap->unpublish(bootstrap->context, provider);
}
int fenestra_native_entry(const FxNativeBootstrap *api, FxNativeExports *exports) {
    if (!api || api->abi != FX_RESIDENT_ABI || api->bytes < sizeof(*api))
        return FX_CAPABILITY;
    bootstrap = api;
    FxProviderDeclaration declaration = {.name = "example.counter", .version = 1u, .call = count};
    FxResult result = api->publish(api->context, &declaration, &provider);
    if (result != FX_OK)
        return (int)result;
    FxUiControl root = {.abi = 1u,
                        .bytes = sizeof(root),
                        .id = 1u,
                        .kind = FX_UI_PAGE,
                        .flags = FX_UI_ROOT | FX_UI_VISIBLE | FX_UI_ENABLED,
                        .label = "Native service"};
    result = fx_native_ui_put(api, &root);
    if (result != FX_OK)
        return (int)result;
    FxUiControl status = {.abi = 1u,
                          .bytes = sizeof(status),
                          .id = 2u,
                          .parent = 1u,
                          .kind = FX_UI_DIALOG,
                          .flags = FX_UI_VISIBLE,
                          .label = "Counter provider",
                          .text = "Available to other modules"};
    result = fx_native_ui_put(api, &status);
    if (result != FX_OK)
        return (int)result;
    exports->purpose = FX_MODULE_SERVICE;
    exports->notify = shutdown;
    return 0;
}
