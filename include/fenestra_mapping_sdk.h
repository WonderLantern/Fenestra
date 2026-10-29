#ifndef FENESTRA_MAPPING_SDK_H
#define FENESTRA_MAPPING_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_mapping.h"
static inline FxResult fx_sdk_mapping(FxSdkClient *c,const FxServiceModule *api,uint64_t binding,
    uint32_t operation,const FxMappingRequest *request) {
    return fx_sdk_platform(c,api,FX_BIND_CALL,binding,operation,0u,request,request?(uint32_t)sizeof(*request):0u);
}
static inline FxResult fx_sdk_mapping_root(FxSdkClient *c,const FxServiceModule *api,uint64_t binding,
    uint32_t operation,const FxMappingRootRequest *request) {
    return fx_sdk_platform(c,api,FX_BIND_CALL,binding,operation,0u,request,request?(uint32_t)sizeof(*request):0u);
}
static inline FxResult fx_sdk_mapping_locked(FxSdkClient *c,const FxServiceModule *api,uint64_t binding,
    uint32_t operation,const FxMappingLockedRequest *request) {
    return fx_sdk_platform(c,api,FX_BIND_CALL,binding,operation,0u,request,request?(uint32_t)sizeof(*request):0u);
}
#endif
