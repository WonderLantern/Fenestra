#include "fenestra_c110_startup.h"
#define FN(a,t) ((t)(uintptr_t)(a))
/* Keep the existing C110 paths and record identity. Policy and short-transfer
 * handling are shared with Z9; every native entry is independently bound. */
const FxStartupFileProfile fx_c110_startup_file_profile={
    FN(0x402e883c,int32_t (*)(const char *,uint32_t,uint32_t)),
    FN(0x402e8a0c,uint32_t (*)(int32_t,void *,uint32_t,uint32_t *)),
    FN(0x402e8ad0,uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *)),
    FN(0x402e889c,uint32_t (*)(int32_t)),
    FN(0x402e8d98,uint32_t (*)(const char *)),
    {"A:\\FENESTRA\\boot.flag","A:\\FENESTRA\\startup.0","A:\\FENESTRA\\startup.1"},
    UINT32_C(0x80000010)
};
void fx_c110_startup_files(FxC110Startup *s) {
    s->files=(FxStartupFiles){.profile=&fx_c110_startup_file_profile,.shutdown=&s->shutdown,.cleanup_window=&s->cleanup_window};
    (void)fx_startup_files(&s->io,&s->files);
}
