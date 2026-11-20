/* Z8 port derived from upstream src/platform/z9_c532/startup_files.c.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#include "fenestra_startup_files.h"
#include "fenestra_z8_file_read.h"
#define FN(a,t) ((t)(uintptr_t)(a))
/* C3.11 wrappers independently exercised against the pinned original MAIN.
 * Separate paths and record identity preserve C110 settings on a shared card. */
const FxStartupFileProfile fx_z8_startup_file_profile={
    FN(FX_Z8_ADDR_402dc960,int32_t (*)(const char *,uint32_t,uint32_t)),
    fx_z8_file_read,
    FN(FX_Z8_ADDR_402dcbf4,uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *)),
    FN(FX_Z8_ADDR_402dc9c0,uint32_t (*)(int32_t)),
    FN(FX_Z8_ADDR_402dceb4,uint32_t (*)(const char *)),
    {"A:\\FENESTRA\\boot-z8.flag","A:\\FENESTRA\\startup-z8.0","A:\\FENESTRA\\startup-z8.1"},
    UINT32_C(0x80000010)
};
