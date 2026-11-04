#include "fenestra_startup_files.h"
#define FN(a,t) ((t)(uintptr_t)(a))
/* Z8 C3.11 card wrappers. Separate startup paths and the Z8 record identity
 * keep other payloads' settings on a shared card. */
const FxStartupFileProfile fx_z8_startup_file_profile={
    FN(0x402daa4c,int32_t (*)(const char *,uint32_t,uint32_t)),
    FN(0x402dab4c,uint32_t (*)(int32_t,void *,uint32_t,uint32_t *)),
    FN(0x402dabb8,uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *)),
    FN(0x402daaac,uint32_t (*)(int32_t)),
    FN(0x402db1ac,uint32_t (*)(const char *)),
    {"A:\\FENESTRA\\boot-z8.flag","A:\\FENESTRA\\startup-z8.0","A:\\FENESTRA\\startup-z8.1"},
    UINT32_C(0x80000010)
};
