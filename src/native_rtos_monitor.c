/* Native EL1 debugger. Direct reads/writes use the requested native address.
 * Address classification and memory permission policy are not admission gates. */
#include "fenestra_ptp.h"
#include "fenestra_memory.h"
#include "fenestra_native_rtos.h"
#include "native_translation_impl.h"
static uint64_t translate(uint64_t address,int write) {
    return fx_translation_probe(address,write?FX_TRANSLATE_EL1_WRITE:FX_TRANSLATE_EL1_READ);
}
/* Debug access is deliberately unqualified: no region/MAIR/identity policy.
 * Hardware translation/access faults are not converted into guessed denials. */
int fx_rtos_read(uint64_t address,uint32_t count,uint8_t *out,uint64_t *par) {
    if(!out || !par || !count || count>1008u || address>UINT64_MAX-count)return 0;
    *par=0;
    const volatile uint8_t *source=(const volatile uint8_t *)(uintptr_t)address;
    for(uint32_t i=0;i<count;++i)out[i]=source[i];
    return 1;
}
uint32_t fx_rtos_debug(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    if(bytes<4u)return FX_PTP_ARGUMENT;
    uint32_t op=fx_ptp_get32(in);in+=4u;bytes-=4u;
    if(op==0u) {
        if(bytes)return FX_PTP_ARGUMENT;
        uint64_t r[10];
        __asm__ volatile("mrs %0,CurrentEL; mrs %1,daif; mrs %2,ttbr0_el1; mrs %3,ttbr1_el1;"
            "mrs %4,tcr_el1; mrs %5,mair_el1; mrs %6,vbar_el1; mrs %7,tpidrro_el0;"
            "mrs %8,cntpct_el0; mrs %9,cntfrq_el0"
            :"=r"(r[0]),"=r"(r[1]),"=r"(r[2]),"=r"(r[3]),"=r"(r[4]),
             "=r"(r[5]),"=r"(r[6]),"=r"(r[7]),"=r"(r[8]),"=r"(r[9])::"memory");
        memset(out,0,96u);memcpy(out,"FXT1",4u);fx_ptp_put32(out+4,1u);
        fx_ptp_put32(out+8,63u); /* monitor, native shell and native call */
        for(uint32_t i=0;i<10u;++i)fx_ptp_put64(out+16u+i*8u,r[i]);
        *n=96u;return FX_PTP_OK;
    }
    if(op==1u) {
        if(bytes!=12u || fx_ptp_get32(in+8)>1u)return FX_PTP_ARGUMENT;
        fx_ptp_put64(out,translate(fx_ptp_get64(in),(int)fx_ptp_get32(in+8)));
        *n=8u;return FX_PTP_OK;
    }
    if(op==2u || op==3u) {
        if(bytes<12u)return FX_PTP_ARGUMENT;
        uint64_t address=fx_ptp_get64(in);uint32_t count=fx_ptp_get32(in+8);
        if(count>1008u || (op==2u?bytes!=12u:bytes!=12u+count))return FX_PTP_ARGUMENT;
        /* Volatile byte accesses avoid compiler-generated overreads. */
        volatile uint8_t *memory=(volatile uint8_t *)(uintptr_t)address;
        if(op==2u){for(uint32_t i=0;i<count;++i)out[i]=memory[i];*n=count;}
        else {for(uint32_t i=0;i<count;++i)memory[i]=in[12u+i];
            __asm__ volatile("dsb sy":::"memory");fx_ptp_put32(out,count);*n=4u;}
        return FX_PTP_OK;
    }
    if(op>=4u && op<=8u)return fx_rtos_request(in-4u,bytes+4u,out,n);
    return FX_PTP_UNSUPPORTED;
}
