#ifndef FX_ZF_C301_LOADED_MODULES_H
#define FX_ZF_C301_LOADED_MODULES_H
#include "fenestra_platform.h"
/* Core-owned append-only resident binding records, not directory entries.
 * Record, platform owner and image are all retained through reboot. Loaded
 * means successful prepared-image binding, not successful INIT/execution.
 * A failed/stopped owner still pins its image and identity; no unload here. */
typedef struct FxZfLoadedModule {
    struct FxZfLoadedModule *next;
    char id[16];
    FxPlatformOwner *owner;
    _Atomic uintptr_t registry;
} FxZfLoadedModule;
typedef struct {
    _Atomic(FxZfLoadedModule *) head;
    _Atomic uint32_t writer;
} FxZfLoadedModules;
enum {
    FX_ZF_MODULES_NONE = 0u,
    FX_ZF_MODULES_DIRECTORY_UNBOUND = 1u,
    FX_ZF_MODULES_SCANNED = 2u,
    FX_ZF_MODULES_RUNTIME_BOUND = 3u
};
FxResult fx_zf_modules_bind(FxZfLoadedModules *,FxZfLoadedModule *,const char *,
                            FxPlatformOwner *,const FxModule *,void *,uint32_t);
uint32_t fx_zf_modules_loaded(const FxZfLoadedModules *,const char *);
FxZfLoadedModules *fx_zf_owner_modules(void);
struct FxNativeModules *fx_zf_owner_modules_manager(void);
#endif
