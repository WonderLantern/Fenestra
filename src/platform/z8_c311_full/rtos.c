/* Z8 port derived from upstream src/platform/z9_c532/rtos.c.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#include "fenestra_native_rtos.h"
#include "ptp.h"
#define FN(a,t) ((t)(uintptr_t)(a))
extern FxRtos *fx_z8_menu_rtos(void);
const FxNativeRtosProfile fx_native_rtos_profile={
    fx_z8_menu_rtos,fx_z8_menu_ptp,
    FN(FX_Z8_ADDR_40c01890,void (*)(const char *,char *)),
    FN(FX_Z8_ADDR_4016a918,int32_t (*)(char *,uint64_t,const char *,const void *)),
    FN(FX_Z8_ADDR_40185064,int32_t (*)(const FxRtosNativeTask *)),
    FN(FX_Z8_ADDR_401855e0,int32_t (*)(int32_t)),
    FN(FX_Z8_ADDR_40187b00,int32_t (*)(uint32_t))};
