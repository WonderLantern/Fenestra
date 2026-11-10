/* Standalone packaged SDK module for the architecture gate fixture. */
#include "fenestra_services.h"

int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->abi!=FX_ABI || base->bytes<sizeof(FxServiceModule))return -1;
    const FxServiceModule *api=(const FxServiceModule *)(const void *)base;
    if(api->service_abi!=FX_SERVICE_ABI || !api->exchange)return -2;
    if(api->capabilities&FX_SDK_SIMD) {
        uint64_t result;
        __asm__ volatile("fmov d0,#3.0; fmov d1,#4.0; fadd d2,d0,d1; fmov %0,d2"
            :"=r"(result)::"v0","v1","v2");
        if(result!=UINT64_C(0x401c000000000000))return -3;
    }
    FxServiceRequest *request=&api->exchange->request;
    request->sequence=api->event;
    request->generation=api->generation;
    request->operation=FX_S_LOG;
    request->arg[0]=api->event;
    request->bytes=0u;
    return 0;
}
