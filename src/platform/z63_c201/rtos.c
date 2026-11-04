/* Original C2.01 command parser, formatter and native task ABI. */
#include "fenestra_native_rtos.h"
#include "ptp.h"
#define FN(a,t) ((t)(uintptr_t)(a))
extern FxRtos *fx_z63_menu_rtos(void);
const FxNativeRtosProfile fx_native_rtos_profile={
    fx_z63_menu_rtos,fx_z63_menu_ptp,
    FN(0x40c01680,void (*)(const char *,char *)),
    FN(0x4016a8a0,int32_t (*)(char *,uint64_t,const char *,const void *)),
    FN(0x4018afb8,int32_t (*)(const FxRtosNativeTask *)),
    FN(0x4018b53c,int32_t (*)(int32_t)),
    FN(0x4018ddc8,int32_t (*)(uint32_t))};
