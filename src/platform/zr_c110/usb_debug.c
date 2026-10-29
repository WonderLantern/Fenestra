#include "fenestra_native_linux.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#include "fenestra_c110_usb.h"
extern FxPtp *fx_c110_menu_ptp(void);
const FxNativeLinuxProfile fx_native_linux_profile={
    fx_c110_menu_usb,fx_c110_menu_ptp,
    FN(UINT64_C(0xa030610c4),const volatile uint8_t *),
    FN(UINT64_C(0xa03062b98),const volatile uint64_t *),
    FN(UINT64_C(0xa03062ba0),const volatile uint32_t *),
    FN(UINT64_C(0xa03062530),_Atomic uintptr_t *),
    FN(UINT64_C(0xa03060230),const volatile uintptr_t *),
    UINT64_C(0xa15c00000),UINT64_C(0xa16c10000),0xfff000u,"C1.10",
    FN(UINT64_C(0x40ea5afc),uint32_t (*)(uint32_t,const uint8_t *)),
    FN(UINT64_C(0x40ea6554),uint8_t *(*)(uint32_t)),
    FN(UINT64_C(0x40ea6704),void (*)(void *)),
    FN(UINT64_C(0x40ea6880),int32_t (*)(const void *)),
    FN(UINT64_C(0x4147f550),int32_t (*)(void *)),
    FN(UINT64_C(0x4147f610),int32_t (*)(void *)),
    FN(UINT64_C(0x4147efe8),int32_t (*)(uintptr_t,const void *,uint32_t)),
    FN(UINT64_C(0x4147e5e0),int32_t (*)(uintptr_t)),
    4u,
    0u, /* Original offset-free returns without freeing offset zero. */
};
void fx_c110_usb_tick(uint64_t s,uint32_t g,int e){fx_linux_tick(s,g,e);}
void fx_c110_usb_revoke(void){fx_linux_revoke();}
uint32_t fx_c110_usb_ready(void){return fx_linux_ready();}
uint32_t fx_c110_usb_request(const uint8_t *i,uint32_t n,uint8_t *o,uint32_t *c){return fx_linux_request(i,n,o,c);}
int fx_c110_usb_quiesced(void){return fx_linux_quiesced();}
