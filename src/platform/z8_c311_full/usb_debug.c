/* Z8 port derived from upstream src/platform/z9_c532/usb_debug.c.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#include "fenestra_native_linux.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#include "ptp.h"
extern FxLinuxBridge *fx_z8_menu_linux(void);
/* queue_block_end is a local acceptance ceiling, not a matched Nikon symbol.
 * The actual queue block must also pass native runtime qualification. */
const FxNativeLinuxProfile fx_native_linux_profile={
    fx_z8_menu_linux,fx_z8_menu_ptp,
    FN(FX_Z8_ADDR_a02eb10c4,const volatile uint8_t *),
    FN(FX_Z8_ADDR_a02eb2878,const volatile uint64_t *),
    FN(FX_Z8_ADDR_a02eb2880,const volatile uint32_t *),
    FN(FX_Z8_ADDR_a02eb2538,_Atomic uintptr_t *),
    FN(FX_Z8_ADDR_a02eb0010,const volatile uintptr_t *),
    FX_Z8_ADDR_a0f000000,FX_Z8_LINUX_QUEUE_BLOCK_END,0x7ff000u,"FXL3",
    FN(FX_Z8_ADDR_40c9ec0c,uint32_t (*)(uint32_t,const uint8_t *)),
    FN(FX_Z8_ADDR_40c9f834,uint8_t *(*)(uint32_t)),
    FN(FX_Z8_ADDR_40c9f9e4,void (*)(void *)),
    FN(FX_Z8_ADDR_40c9fb3c,int32_t (*)(const void *)),
    FN(FX_Z8_ADDR_4127d6f0,int32_t (*)(void *)),
    FN(FX_Z8_ADDR_4127d7b0,int32_t (*)(void *)),
    FN(FX_Z8_ADDR_4127d188,int32_t (*)(uintptr_t,const void *,uint32_t)),
    FN(FX_Z8_ADDR_4127c780,int32_t (*)(uintptr_t)),
    4u,
    1u, /* Original offset-free includes the pool base at offset zero. */
};
