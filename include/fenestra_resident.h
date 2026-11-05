#ifndef FENESTRA_RESIDENT_H
#define FENESTRA_RESIDENT_H
#include "fenestra_extensions.h"
#include "fenestra_platform.h"
#include "fenestra_stream.h"

#define FX_RESIDENT_ABI 1u
enum {
    FX_RESIDENT_EMPTY,
    FX_RESIDENT_LOADING,
    FX_RESIDENT_RUNNING,
    FX_RESIDENT_STOPPING,
    FX_RESIDENT_STOPPED,
    FX_RESIDENT_FAULT
};
enum { FX_RESIDENT_SHUTDOWN = 1u, FX_RESIDENT_PANIC };
typedef struct FxResident FxResident;
typedef struct FxNativeBootstrap FxNativeBootstrap;
typedef struct {
    uint32_t abi, bytes, purpose, flags;
    void *context;
    /* Original Loader task context. Return BUSY to retry on a later poll.
     * This is a notification; it never revokes native execution or pointers. */
    FxResult (*notify)(void *, uint32_t event);
} FxNativeExports;
struct FxNativeBootstrap {
    uint32_t abi, bytes, owner, generation;
    void *context;
    const char *model, *firmware;
    uint8_t target[32];
    FxResult (*resolve)(void *, const char *, uint32_t minimum, FxBindingInfo *);
    /* Direct, serialized ordinary service adapter. The adapter assigns sequence
     * and generation while holding the module request lock. This optional adapter does not proxy native
     * calls. */
    FxResult (*request)(void *, const FxServiceRequest *, FxServiceReply *);
    FxResult (*fault)(void *, FxResult, uint64_t detail);
    FxResult (*publish)(void *, const FxProviderDeclaration *, uint64_t *);
    FxResult (*unpublish)(void *, uint64_t);
    FxResult (*find_provider)(void *, const FxProviderLookup *, FxProviderInfo *);
    FxResult (*stream)(void *, uint64_t, FxStreamInfo *);
};
_Static_assert(sizeof(FxNativeBootstrap) == 128u, "native bootstrap ABI 1");
_Static_assert(sizeof(FxNativeExports) == 32u, "native exports ABI 1");
typedef int (*FxNativeModuleEntry)(const FxNativeBootstrap *, FxNativeExports *);
typedef struct {
    FxIndexMemory memory, source_memory;
    void *context;
    FxResult (*prepare_owner)(void *, FxPlatformOwner *);
    void (*refresh_ui)(FxPlatformOwner *);
    FxResult (*protect)(void *, FxPlatformOwner *, uint8_t *, uint32_t, uint32_t permissions);
    /* Test/architecture adapter; production invokes the relocated entry. */
    int (*enter)(void *, uintptr_t, const FxNativeBootstrap *, FxNativeExports *);
} FxResidentHost;
FxResult fx_resident_start(FxResident **, const FxPlatform *, const FxDiscoveredModule *, uint32_t owner,
                           uint32_t generation, const FxModuleDirectory *, FxResidentHost);
FxPlatformOwner *fx_resident_owner(FxResident *);
const char *fx_resident_identity(const FxResident *);
uint32_t fx_resident_phase(const FxResident *);
FxResult fx_resident_error(const FxResident *);
uint64_t fx_resident_detail(const FxResident *);
void fx_resident_request_stop(FxResident *, uint32_t panic);
FxResult fx_resident_poll(FxResident *);
/* Verified original RTOS functions, shared by both profiles. */
FxResult fx_native_resident_protect(void *, FxPlatformOwner *, uint8_t *, uint32_t, uint32_t);
FxResidentHost fx_native_resident_host(const FxPlatform *);
#endif
