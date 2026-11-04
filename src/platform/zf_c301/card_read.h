#ifndef FENESTRA_ZF_C301_CARD_READ_H
#define FENESTRA_ZF_C301_CARD_READ_H
#include <stdint.h>
typedef uint32_t (*FxZfReadNative)(int32_t,void *,uint32_t,uint32_t *);
uint32_t fx_zf_card_read_cached(FxZfReadNative,int32_t,void *,uint32_t,uint32_t *);
uint32_t fx_zf_card_read(int32_t,void *,uint32_t,uint32_t *);
#endif
