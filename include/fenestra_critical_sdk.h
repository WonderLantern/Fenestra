#ifndef FENESTRA_CRITICAL_SDK_H
#define FENESTRA_CRITICAL_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_critical.h"
static inline FxResult fx_sdk_critical_call(FxSdkClient *c,const FxServiceModule *a,
    uint64_t critical,uint64_t lock,uint64_t function,const void *arguments,uint32_t bytes) {
    if(!arguments || (bytes!=64u && bytes!=sizeof(FxNativeCallFrame)))return FX_ARGUMENT;
    FxCriticalCall call={.lock=lock,.binding=function,.call_bytes=bytes};
    const uint8_t *source=arguments;for(uint32_t i=0;i<bytes;++i)call.call[i]=source[i];
    return fx_sdk_platform(c,a,FX_BIND_CALL,critical,FX_CRITICAL_CALL,0u,&call,sizeof(call));
}
#endif
