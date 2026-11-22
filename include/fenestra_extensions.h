#ifndef FENESTRA_EXTENSIONS_H
#define FENESTRA_EXTENSIONS_H
#include "fenestra_platform.h"
#include "fenestra_stream.h"

/* Optional native providers extend ordinary modules without core opcodes.
 * Names describe interfaces; providers are registered by modules at runtime.
 * All published descriptors, handlers and contexts have reboot lifetime. */
#define FX_PROVIDER_ABI 1u
typedef struct {
    uint32_t abi, bytes, owner, generation, purpose, entry_abi, image_bytes, capabilities;
    uint64_t image;
    uint8_t target[32];
} FxModuleSelfInfo;
typedef struct FxExtensionDomain FxExtensionDomain;
typedef struct {
    uint32_t abi, bytes, owner, generation;
} FxProviderClient;
typedef FxResult (*FxProviderCall)(void *, const FxProviderClient *, uint64_t, const void *, uint32_t,
                                   FxServiceReply *);
typedef struct {
    const char *name;
    uint32_t version;
    uint64_t features;
    void *context;
    FxProviderCall call;
} FxProviderDeclaration;
typedef struct {
    uint64_t token, features;
    uint32_t version, owner, generation, active, calls, reserved;
    char name[64];
} FxProviderInfo;
typedef struct {
    char name[64];
    uint32_t minimum, maximum;
    uint64_t features;
} FxProviderLookup;
typedef struct {
    uint64_t token, operation;
} FxProviderRequest;
enum { FX_PROVIDER_QUERY, FX_PROVIDER_ENUM, FX_PROVIDER_LOOKUP, FX_PROVIDER_CALL, FX_PROVIDER_REMOVE };
FxExtensionDomain *fx_extensions_create(FxIndexMemory, uint32_t budget);
FxExtensionDomain *fx_native_extensions(FxPlatformOwner *);
FxResult fx_provider_publish(FxExtensionDomain *, FxPlatformOwner *, const FxProviderDeclaration *,
                             uint64_t *);
FxResult fx_provider_lookup(FxExtensionDomain *, const FxProviderLookup *, FxProviderInfo *);
FxResult fx_provider_remove(FxExtensionDomain *, FxPlatformOwner *, uint64_t);
FxResult fx_provider_call(FxExtensionDomain *, FxPlatformOwner *, uint64_t, uint64_t, const void *, uint32_t,
                          FxServiceReply *);
FxResult fx_extensions_resource(void *, uint32_t, const FxNamedBinding *, FxPlatformOwner *, uint64_t,
                                uint64_t *, const FxServiceRequest *, FxServiceReply *);
void fx_extensions_pulse(FxPlatformOwner *, uint64_t milliseconds);
FxResult fx_extensions_stop(FxPlatformOwner *);

enum { FX_STREAM_QUERY, FX_STREAM_REGISTER, FX_STREAM_LOOKUP, FX_STREAM_REMOVE };
typedef struct {
    uint64_t address, epoch;
    uint32_t bytes, reserved;
    char name[64];
} FxStreamRegistration;
typedef struct {
    uint64_t token, address, epoch;
    uint32_t bytes, slots, payload, owner, generation, active, clock_id, clock_hz;
    char name[64];
} FxStreamInfo;
FxResult fx_extensions_stream(FxExtensionDomain *, uint64_t, FxStreamInfo *);

#define FX_LOG_TEXT 160u
enum { FX_LOG_QUERY, FX_LOG_WRITE, FX_LOG_READ };
enum { FX_LOG_DEBUG, FX_LOG_INFO, FX_LOG_WARNING, FX_LOG_ERROR };
typedef struct {
    uint64_t sequence, time_ms;
    uint32_t owner, generation, level, bytes;
    char text[FX_LOG_TEXT];
} FxLogRecord;
typedef struct {
    uint32_t level, bytes;
    char text[FX_LOG_TEXT];
} FxLogWrite;
typedef struct {
    uint64_t oldest, next, dropped;
    uint32_t capacity, record_bytes;
} FxLogInfo;

enum { FX_TIME_QUERY, FX_TIME_CREATE, FX_TIME_ARM, FX_TIME_POLL, FX_TIME_CANCEL, FX_TIME_RELEASE };
typedef struct {
    uint64_t token, deadline_ms, period_ms;
} FxTimerRequest;
typedef struct {
    uint64_t token, deadline_ms, period_ms, expirations;
    uint32_t armed, reserved;
} FxTimerInfo;
/* Absolute millisecond timers run on the ordinary worker. This is not an IRQ
 * deadline guarantee. Raw hardware timers belong to native providers. */
#endif
