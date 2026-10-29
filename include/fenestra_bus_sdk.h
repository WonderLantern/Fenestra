#ifndef FENESTRA_BUS_SDK_H
#define FENESTRA_BUS_SDK_H
#include "fenestra_sdk.h"
/* Asynchronous copied messages. Each request must be taken before submitting
 * another; BUSY/EMPTY are retryable on a later module invocation, never spin. */
static inline FxResult fx_sdk_bus_query(FxSdkClient *c,const FxServiceModule *a,uint64_t bus) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,bus,FX_BUS_QUERY,0,0,0);
}
static inline FxResult fx_sdk_bus_enum(FxSdkClient *c,const FxServiceModule *a,uint64_t bus,uint32_t cursor) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,bus,FX_BUS_ENUM,cursor,0,0);
}
/* endpoint=0 queries the shared bus; otherwise copies this endpoint's limits. */
static inline FxResult fx_sdk_bus_capacity(FxSdkClient *c,const FxServiceModule *a,uint64_t bus,uint64_t endpoint) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,bus,FX_BUS_CAPACITY,endpoint,0,0);
}
static inline FxResult fx_sdk_bus_reserve(FxSdkClient *c,const FxServiceModule *a,uint64_t bus,uint64_t endpoint,uint32_t capacity) {
    FxBusQueueRequest r={endpoint,capacity,0u};
    return fx_sdk_platform(c,a,FX_BIND_CALL,bus,FX_BUS_RESERVE,0,&r,sizeof(r));
}
static inline FxResult fx_sdk_bus_named(FxSdkClient *c,const FxServiceModule *a,uint64_t bus,
    uint32_t operation,const char *name,uint32_t version) {
    if(!name || !version || (operation!=FX_BUS_PUBLISH && operation!=FX_BUS_LOOKUP))return FX_ARGUMENT;
    FxEndpointRequest r={.version=version};uint32_t n=0;
    while(n<sizeof(r.name) && name[n])++n;
    if(!n || n==sizeof(r.name))return FX_ARGUMENT;
    for(uint32_t i=0;i<n;++i)r.name[i]=name[i];
    return fx_sdk_platform(c,a,FX_BIND_CALL,bus,operation,0,&r,sizeof(r));
}
static inline FxResult fx_sdk_bus_send(FxSdkClient *c,const FxServiceModule *a,uint64_t bus,
    uint64_t endpoint,uint64_t correlation,const void *data,uint32_t bytes) {
    if(!endpoint || bytes>FX_BUS_BYTES || (bytes && !data))return FX_ARGUMENT;
    FxModuleMessage r={.endpoint=endpoint,.correlation=correlation,.bytes=bytes};
    for(uint32_t i=0;i<bytes;++i)r.data[i]=((const uint8_t *)data)[i];
    return fx_sdk_platform(c,a,FX_BIND_CALL,bus,FX_BUS_SEND,0,&r,sizeof(r));
}
static inline FxResult fx_sdk_bus_poll(FxSdkClient *c,const FxServiceModule *a,uint64_t bus,uint64_t endpoint) {
    FxModuleMessage r={.endpoint=endpoint};
    return fx_sdk_platform(c,a,FX_BIND_CALL,bus,FX_BUS_POLL,0,&r,sizeof(r));
}
static inline FxResult fx_sdk_bus_remove(FxSdkClient *c,const FxServiceModule *a,uint64_t bus,uint64_t endpoint) {
    FxModuleMessage r={.endpoint=endpoint};
    return fx_sdk_platform(c,a,FX_BIND_CALL,bus,FX_BUS_REMOVE,0,&r,sizeof(r));
}
#endif
