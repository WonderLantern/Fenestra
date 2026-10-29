/* Legacy entry names; both targets execute the shared retained-job engine. */
#include "fenestra_c110_rtos.h"
#define FN(a,t) ((t)(uintptr_t)(a))
const FxNativeRtosProfile fx_native_rtos_profile={
    fx_c110_menu_rtos,fx_c110_menu_ptp,
    FN(UINT64_C(0x40e018a8),void (*)(const char *,char *)),
    FN(UINT64_C(0x4016a918),int32_t (*)(char *,uint64_t,const char *,const void *)),
    FN(UINT64_C(0x4018507c),int32_t (*)(const FxRtosNativeTask *)),
    FN(UINT64_C(0x401855f8),int32_t (*)(int32_t)),
    FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))};
void fx_c110_rtos_tick(uint64_t session,uint32_t generation,int enabled) {(void)session;(void)generation;(void)enabled;}
void fx_c110_rtos_revoke(void) {fx_rtos_revoke();}
int fx_c110_rtos_quiesced(void) {return fx_rtos_quiesced();}
uint32_t fx_c110_rtos_vprintf(const char *f,const void *a,uintptr_t sp) {return fx_rtos_vprintf(f,a,sp);}
uint32_t fx_c110_rtos_request(const uint8_t *in,uint32_t b,uint8_t *out,uint32_t *n) {return fx_rtos_request(in,b,out,n);}
