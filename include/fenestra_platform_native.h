#ifndef FENESTRA_PLATFORM_NATIVE_H
#define FENESTRA_PLATFORM_NATIVE_H
#include "fenestra_platform.h"
#include "fenestra_capacity.h"
FxPlatformIO fx_platform_native_io(void);
/* IRQ-capable atomic notification; never renders or takes native GUI locks. */
void fx_native_module_fault(FxPlatformOwner *,FxResult);
/* Present only when built with a generated, full-image-verified profile. */
const FxPlatform *fx_platform_target(void);
/* Profile-backed native domains; unknown native totals stay zero. This is
 * provider configuration, not an installed-module or subscription limit. */
typedef struct {FxCapacityDomain card,buffers,events,mappings,ui_pages,ui_texts;} FxNativeCapacityDomains;
struct FxNativeUiSpace;
FxResult fx_platform_capacity_init(FxCapacityRegistry *,FxNativeCapacityDomains *,FxIndexMemory,FxAllocationObserver,struct FxNativeUiSpace *);
#endif
