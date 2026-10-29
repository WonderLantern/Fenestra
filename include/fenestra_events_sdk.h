#ifndef FENESTRA_EVENTS_SDK_H
#define FENESTRA_EVENTS_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_events.h"
static inline FxResult fx_sdk_event(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint32_t op,uint64_t event,uint32_t bits,uint32_t mode) {
    FxEventRequest r={event,bits,mode};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,op,0u,op==FX_EVENT_QUERY?0:&r,op==FX_EVENT_QUERY?0u:sizeof(r));
}
#endif
