/* C2.01 original Ch16 FIFO, shared allocator and Ch00 receive callback. */
#include "fenestra_native_linux.h"
#include "ptp.h"
#define FN(a,t) ((t)(uintptr_t)(a))
extern FxLinuxBridge *fx_z63_menu_linux(void);
const FxNativeLinuxProfile fx_native_linux_profile={
    fx_z63_menu_linux,fx_z63_menu_ptp,
    FN(0xa02fc7b90,const volatile uint8_t *),
    FN(0xa02fc7b98,const volatile uint64_t *),
    FN(0xa02fc7ba0,const volatile uint32_t *),
    FN(0xa02fc7530,_Atomic uintptr_t *),
    FN(0xa02fc5230,const volatile uintptr_t *),
    UINT64_C(0xa14c00000),UINT64_C(0xa15bff000),0xfff000u,"FXL3",
    FN(0x40c9ead8,uint32_t (*)(uint32_t,const uint8_t *)),
    FN(0x40c9f4d4,uint8_t *(*)(uint32_t)),
    FN(0x40c9f6a4,void (*)(void *)),
    FN(0x40c9f864,int32_t (*)(const void *)),
    FN(0x41635980,int32_t (*)(void *)),
    FN(0x41635a38,int32_t (*)(void *)),
    FN(0x41635438,int32_t (*)(uintptr_t,const void *,uint32_t)),
    FN(0x41634a40,int32_t (*)(uintptr_t)),
    4u,0u /* Original offset-free returns immediately for offset zero. */};
