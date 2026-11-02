#ifndef FX_ZF_C301_OWNER_PUBLISH_H
#define FX_ZF_C301_OWNER_PUBLISH_H
#include "owner_storage.h"
typedef struct {
    void *context;
    void *(*allocate)(void *,uint32_t);
    void (*release)(void *,void *);
} FxZfOwnerMemory;
/* Experimental native publication requires an aligned normal-RAM slot.
 * All candidate fields are initialized before a single release CAS. Only an
 * unpublished loser may be released. Original descriptor stays alive. Caller
 * validates execution context and excludes native descriptor/source mutation;
 * this only serializes competing Fenestra publishers, not arbitrary Nikon code.
 * No retained owner can be uninstalled, reused or freed by this API. */
FxResult fx_zf_owner_publish(FxNativePage *,const FxNativeDescriptor *,const FxZfOwnerMemory *);
FxResult fx_zf_owner_bootstrap(void);
FxResult fx_zf_owner_module_action(FxPlatformOwner *,uint32_t);
/* Explicit research scheduling policy; no camera default is selected here. */
FxResult fx_zf_owner_start_native_task(uint32_t priority,uint32_t mode,uint32_t delay_ticks);
#endif
