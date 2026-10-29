#include "fenestra_native_linux.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#include "ptp.h"
extern FxLinuxBridge *fx_z9_menu_linux(void);
const FxNativeLinuxProfile fx_native_linux_profile={
    fx_z9_menu_linux,fx_z9_menu_ptp,
    FN(UINT64_C(0xa02eb10c4),const volatile uint8_t *),
    FN(UINT64_C(0xa02eb2878),const volatile uint64_t *),
    FN(UINT64_C(0xa02eb2880),const volatile uint32_t *),
    FN(UINT64_C(0xa02eb2538),_Atomic uintptr_t *),
    FN(UINT64_C(0xa02eb0010),const volatile uintptr_t *),
    UINT64_C(0xa0f000000),UINT64_C(0xa0f801000),0x7ff000u,"FXL3",
    FN(UINT64_C(0x40c9ec0c),uint32_t (*)(uint32_t,const uint8_t *)),
    FN(UINT64_C(0x40c9f834),uint8_t *(*)(uint32_t)),
    FN(UINT64_C(0x40c9f9e4),void (*)(void *)),
    FN(UINT64_C(0x40c9fb3c),int32_t (*)(const void *)),
    FN(UINT64_C(0x4127d6f0),int32_t (*)(void *)),
    FN(UINT64_C(0x4127d7b0),int32_t (*)(void *)),
    FN(UINT64_C(0x4127d188),int32_t (*)(uintptr_t,const void *,uint32_t)),
    FN(UINT64_C(0x4127c780),int32_t (*)(uintptr_t)),
    4u,
    1u, /* Original offset-free includes the pool base at offset zero. */
};
