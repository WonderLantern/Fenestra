#ifndef FENESTRA_CORE_PORT_PROFILES_H
#define FENESTRA_CORE_PORT_PROFILES_H
#include <stdint.h>

#ifdef FX_UNIVERSAL_CORE
#include "fenestra_core_port.h"
#include "fenestra_native_display.h"
#include "fenestra_native_linux.h"
#include "fenestra_native_ptp.h"
#include "fenestra_native_rtos.h"
#include "fenestra_native_ui_dispatch.h"

#define FX_PROFILE_ACCESSOR(object,accessor,type,slot) const type *accessor(void);
FX_CORE_PORT_PROFILES(FX_PROFILE_ACCESSOR)
#undef FX_PROFILE_ACCESSOR
#define fx_native_display_profile (*fx_core_port_display())
#define fx_native_linux_profile (*fx_core_port_linux())
#define fx_native_ptp_profile (*fx_core_port_ptp())
#define fx_native_rtos_profile (*fx_core_port_rtos())
#define fx_native_ui_dispatch_profile (*fx_core_port_ui())
#endif

static inline int fx_native_ram_contains(const void *ptr,uint32_t bytes,uintptr_t alignment) {
#ifdef FX_UNIVERSAL_CORE
    const FxNativeDisplayProfile *profile=fx_core_port_display();
    uint64_t begin=profile->ram_begin,end=profile->ram_end;
#else
    uint64_t begin=UINT64_C(0xa00000000),end=UINT64_C(0xa40000000);
#endif
    uintptr_t address=(uintptr_t)ptr;
    return ptr && !(address&(alignment-1u)) && address>=begin &&
        address<=end && bytes<=end-address;
}
#endif
