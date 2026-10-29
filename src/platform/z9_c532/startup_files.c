#include "fenestra_startup_files.h"
#define FN(a,t) ((t)(uintptr_t)(a))
/* C5.32 wrappers independently exercised against the pinned original MAIN.
 * Separate paths and record identity preserve C110 settings on a shared card. */
const FxStartupFileProfile fx_z9_startup_file_profile={
    FN(0x402dc960,int32_t (*)(const char *,uint32_t,uint32_t)),
    FN(0x402dcb30,uint32_t (*)(int32_t,void *,uint32_t,uint32_t *)),
    FN(0x402dcbf4,uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *)),
    FN(0x402dc9c0,uint32_t (*)(int32_t)),
    FN(0x402dceb4,uint32_t (*)(const char *)),
    {"A:\\FENESTRA\\boot-z9.flag","A:\\FENESTRA\\startup-z9.0","A:\\FENESTRA\\startup-z9.1"},
    UINT32_C(0x80000010)
};
