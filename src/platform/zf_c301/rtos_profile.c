#if !defined(FX_ZF_EXPERIMENTAL_RTOS_PROFILE) || FX_ZF_EXPERIMENTAL_RTOS_PROFILE != 1
#error "Zf RTOS debug profile requires the hash-checked experimental build"
#endif
#include "fenestra_native_rtos.h"
#include "fx_zf_rtos_profile.h"
#include "ptp.h"
extern FxRtos *fx_zf_owner_rtos(void);
#define FN(a,t) ((t)(uintptr_t)(a))
const FxNativeRtosProfile fx_native_rtos_profile={
    fx_zf_owner_rtos,fx_zf_ptp_owner,
    FN(FX_ZF_RTOS_COMMAND,void (*)(const char *,char *)),
    FN(FX_ZF_RTOS_FORMAT,int32_t (*)(char *,uint64_t,const char *,const void *)),
    FN(FX_ZF_RTOS_CREATE,int32_t (*)(const FxRtosNativeTask *)),
    FN(FX_ZF_RTOS_START,int32_t (*)(int32_t)),
    FN(FX_ZF_RTOS_DELAY,int32_t (*)(uint32_t))};
