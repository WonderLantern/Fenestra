#ifndef FENESTRA_EXTENSIONS_SDK_H
#define FENESTRA_EXTENSIONS_SDK_H
#include "fenestra_extensions.h"
#include "fenestra_sdk.h"
static inline FxResult fx_sdk_provider_find(FxSdkClient *c, const FxServiceModule *a, uint64_t binding,
                                            const char *name, uint32_t minimum, uint32_t maximum,
                                            uint64_t features) {
    FxProviderLookup p = {.minimum = minimum, .maximum = maximum, .features = features};
    uint32_t n = 0u;
    if (!name)
        return FX_ARGUMENT;
    while (n < sizeof(p.name) && name[n]) {
        p.name[n] = name[n];
        ++n;
    }
    if (!n || n == sizeof(p.name))
        return FX_ARGUMENT;
    return fx_sdk_platform(c, a, FX_BIND_CALL, binding, FX_PROVIDER_LOOKUP, 0u, &p, sizeof(p));
}
static inline FxResult fx_sdk_provider_call(FxSdkClient *c, const FxServiceModule *a, uint64_t binding,
                                            uint64_t provider, uint64_t operation, const void *data,
                                            uint32_t bytes) {
    struct {
        FxProviderRequest request;
        uint8_t data[FX_SERVICE_BYTES - sizeof(FxProviderRequest)];
    } p = {.request = {provider, operation}};
    if (bytes > sizeof(p.data) || (bytes && !data))
        return FX_ARGUMENT;
    for (uint32_t i = 0u; i < bytes; ++i)
        p.data[i] = ((const uint8_t *)data)[i];
    return fx_sdk_platform(c, a, FX_BIND_CALL, binding, FX_PROVIDER_CALL, 0u, &p,
                           (uint32_t)sizeof(p.request) + bytes);
}
static inline FxResult fx_sdk_stream_register(FxSdkClient *c, const FxServiceModule *a, uint64_t binding,
                                              FxStream *stream, const char *name) {
    if (!stream || !name)
        return FX_ARGUMENT;
    FxStreamRegistration p = {.address = (uintptr_t)stream, .epoch = stream->epoch, .bytes = stream->bytes};
    uint32_t n = 0u;
    while (n < sizeof(p.name) && name[n]) {
        p.name[n] = name[n];
        ++n;
    }
    if (!n || n == sizeof(p.name))
        return FX_ARGUMENT;
    return fx_sdk_platform(c, a, FX_BIND_CALL, binding, FX_STREAM_REGISTER, 0u, &p, sizeof(p));
}
static inline FxResult fx_sdk_log_write(FxSdkClient *c, const FxServiceModule *a, uint64_t binding,
                                        uint32_t level, const char *text) {
    FxLogWrite p = {.level = level};
    if (!text)
        return FX_ARGUMENT;
    while (p.bytes < FX_LOG_TEXT && text[p.bytes]) {
        p.text[p.bytes] = text[p.bytes];
        ++p.bytes;
    }
    if (p.bytes == FX_LOG_TEXT && text[p.bytes])
        return FX_BOUNDS;
    return fx_sdk_platform(c, a, FX_BIND_CALL, binding, FX_LOG_WRITE, 0u, &p, sizeof(p));
}
static inline FxResult fx_sdk_timer_arm(FxSdkClient *c, const FxServiceModule *a, uint64_t binding,
                                        uint64_t timer, uint64_t deadline_ms, uint64_t period_ms) {
    FxTimerRequest p = {timer, deadline_ms, period_ms};
    return fx_sdk_platform(c, a, FX_BIND_CALL, binding, FX_TIME_ARM, 0u, &p, sizeof(p));
}
#endif
