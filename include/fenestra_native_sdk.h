#ifndef FENESTRA_NATIVE_SDK_H
#define FENESTRA_NATIVE_SDK_H
#include "fenestra_resident.h"
static inline FxResult fx_native_request(const FxNativeBootstrap *api, uint32_t operation,
                                         const uint64_t args[4], const void *data, uint32_t bytes,
                                         FxServiceReply *out) {
    if (!api || api->abi != FX_RESIDENT_ABI || api->bytes < sizeof(*api) || !out ||
        bytes > FX_SERVICE_BYTES || (bytes && !data))
        return FX_ARGUMENT;
    FxServiceRequest q = {.operation = operation, .bytes = bytes};
    for (uint32_t i = 0u; i < 4u; ++i)
        q.arg[i] = args ? args[i] : 0u;
    for (uint32_t i = 0u; i < bytes; ++i)
        q.data[i] = ((const uint8_t *)data)[i];
    return api->request(api->context, &q, out);
}
static inline FxResult fx_native_ui_put(const FxNativeBootstrap *api, const FxUiControl *control) {
    FxServiceReply reply;
    const uint64_t args[4] = {FX_UI_PUT, 0u, 0u, 0u};
    return control ? fx_native_request(api, FX_S_UI, args, control, sizeof(*control), &reply) : FX_ARGUMENT;
}
#endif
