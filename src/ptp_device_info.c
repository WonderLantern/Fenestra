#include "fenestra_ptp.h"
#include "fenestra_memory.h"
uint32_t fx_ptp_extend_operations(uint8_t *out,uint32_t capacity,const uint8_t *in,uint32_t input_bytes) {
    if(!out || !in || input_bytes<4u)return 0;
    uint32_t count=fx_ptp_get32(in);
    if(count>(input_bytes-4u)/2u)return 0;
    uint32_t missing=15u;
    for(uint32_t i=0;i<count;++i) {
        uint32_t op=(uint32_t)in[4u+i*2u]|((uint32_t)in[5u+i*2u]<<8);
        if(op>=FX_PTP_HELLO && op<=FX_PTP_CONTROL)missing&=~(1u<<(op-FX_PTP_HELLO));
    }
    uint32_t extra=(missing&1u)+((missing>>1)&1u)+((missing>>2)&1u)+((missing>>3)&1u);
    if(count>(UINT32_MAX-4u)/2u-extra)return 0;
    uint32_t original=4u+count*2u,total=original+extra*2u;
    if(total>capacity || (uintptr_t)out>UINTPTR_MAX-total || (uintptr_t)in>UINTPTR_MAX-original)return 0;
    if(out!=in && (uintptr_t)out<(uintptr_t)in+original && (uintptr_t)in<(uintptr_t)out+total)return 0;
    if(out!=in)memcpy(out,in,original);
    uint32_t at=original;
    for(uint32_t i=0;i<4u;++i)if(missing&(1u<<i)) {
        uint32_t op=FX_PTP_HELLO+i;out[at++]=(uint8_t)op;out[at++]=(uint8_t)(op>>8);
    }
    fx_ptp_put32(out,count+extra);return total;
}
