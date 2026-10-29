/* Existing native readers keep their ABI and now share both-model code. */
#include "fenestra_c110_rtos.h"
int fx_c110_rtos_read_normal(uint64_t a,uint32_t n,uint8_t *out,uint64_t *par){return fx_rtos_read(a,n,out,par);}
int fx_c110_rtos_read_video(uint64_t a,uint32_t n,uint8_t *out,uint64_t *par){return fx_rtos_read(a,n,out,par);}
int fx_c110_rtos_read_native(uint64_t a,uint32_t n,uint8_t *out,uint64_t *par){return fx_rtos_read(a,n,out,par);}
int fx_c110_rtos_native_span(uint64_t a,uint32_t n,uint64_t *identity,uint64_t *par) {
    if(!identity || !par || !n || n>1008u || a>UINT64_MAX-n)return 0;
    *identity=a;*par=0;return 1;
}
uint32_t fx_c110_rtos_debug(const uint8_t *in,uint32_t b,uint8_t *out,uint32_t *n) {return fx_rtos_debug(in,b,out,n);}
