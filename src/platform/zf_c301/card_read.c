#include "card_read.h"

/* Own-Zf 408cafb8 reads partial sectors through 408ceb50: cached-sector CPU
 * copy followed by 402ec25c (clean/invalidate). A request smaller than the
 * 512-byte sector cannot use the full-sector direct DMA path. This keeps
 * package bytes coherent when the destination shares a cache line with
 * manager metadata. Preserve status, actual length and EOF unchanged. */
uint32_t fx_zf_card_read_cached(FxZfReadNative native,int32_t fd,void *data,
                              uint32_t bytes,uint32_t *done) {
    uint8_t *output=data;
    if(!done)return UINT32_C(0x80000001);
    *done=0u;
    if(!native || (!data && bytes))return UINT32_C(0x80000001);
    while(*done<bytes) {
        uint32_t room=bytes-*done,actual=0u;
        if(room>511u)room=511u;
        uint32_t result=native(fd,output+*done,room,&actual);
        if(actual>room)return UINT32_C(0x80000001);
        *done+=actual;
        if(result || actual<room)return result;
    }
    return 0u;
}

uint32_t fx_zf_card_read(int32_t fd,void *data,uint32_t bytes,uint32_t *done) {
    return fx_zf_card_read_cached((FxZfReadNative)(uintptr_t)UINT64_C(0x402e67ec),
                                 fd,data,bytes,done);
}
