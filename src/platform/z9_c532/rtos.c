#include "fenestra_native_rtos.h"
#include "ptp.h"
#define FN(a,t) ((t)(uintptr_t)(a))
extern FxRtos *fx_z9_menu_rtos(void);
const FxNativeRtosProfile fx_native_rtos_profile={
    fx_z9_menu_rtos,fx_z9_menu_ptp,
    FN(UINT64_C(0x40c01890),void (*)(const char *,char *)),
    FN(UINT64_C(0x4016a918),int32_t (*)(char *,uint64_t,const char *,const void *)),
    FN(UINT64_C(0x40185064),int32_t (*)(const FxRtosNativeTask *)),
    FN(UINT64_C(0x401855e0),int32_t (*)(int32_t)),
    FN(UINT64_C(0x40187b00),int32_t (*)(uint32_t))};
