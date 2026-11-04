#if !defined(FX_ZF_EXPERIMENTAL_LINUX_PROFILE) || FX_ZF_EXPERIMENTAL_LINUX_PROFILE != 1
#error "Zf Linux bridge profile is experimental; no transport activation"
#endif
#include "fenestra_native_linux.h"
#include "fx_zf_linux_profile.h"
#include "ptp.h"
extern FxLinuxBridge *fx_zf_owner_linux(void);
#define FN(a,t) ((t)(uintptr_t)(a))
/* Own original allocator globals/queue0 slot and object callback. Pool bounds
 * come from the archived own initialization, not Z9. Installation still checks
 * current ready/base/size. Queue blocks outside this pool are rejected. */
const FxNativeLinuxProfile fx_native_linux_profile={
    fx_zf_owner_linux,fx_zf_ptp_owner,
    FN(UINT64_C(0xa02fb60c4),const volatile uint8_t *),
    FN(UINT64_C(0xa02fb7b98),const volatile uint64_t *),
    FN(UINT64_C(0xa02fb7ba0),const volatile uint32_t *),
    FN(UINT64_C(0xa02fb7530),_Atomic uintptr_t *),
    FN(UINT64_C(0xa02fb5230),const volatile uintptr_t *),
    UINT64_C(0xa14c00000),UINT64_C(0xa15bff000),0xfff000u,"FXL3",
    FN(FX_ZF_LINUX_RECEIVE,uint32_t (*)(uint32_t,const uint8_t *)),
    FN(FX_ZF_LINUX_ALLOCATE,uint8_t *(*)(uint32_t)),
    FN(FX_ZF_LINUX_RELEASE,void (*)(void *)),
    FN(FX_ZF_LINUX_OFFSET,int32_t (*)(const void *)),
    FN(FX_ZF_LINUX_TRY_LOCK,int32_t (*)(void *)),
    FN(FX_ZF_LINUX_UNLOCK,int32_t (*)(void *)),
    FN(FX_ZF_LINUX_ENQUEUE,int32_t (*)(uintptr_t,const void *,uint32_t)),
    FN(FX_ZF_LINUX_NOTIFY,int32_t (*)(uintptr_t)),
    4u,0u /* Original release(POOL) returns without freeing; offset0 excluded. */
};
