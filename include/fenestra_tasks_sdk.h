#ifndef FENESTRA_TASKS_SDK_H
#define FENESTRA_TASKS_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_tasks.h"
static inline FxResult fx_sdk_task_post(FxSdkClient *c,const FxServiceModule *a,uint64_t tasks,
    uint64_t function,const void *arguments,uint32_t bytes) {
    if(!arguments || (bytes!=64u && bytes!=sizeof(FxNativeCallFrame)))return FX_ARGUMENT;
    FxTaskPost p={.context="original.ui",.binding=function,.call_bytes=bytes};
    const uint8_t *source=arguments;for(uint32_t i=0;i<bytes;++i)p.call[i]=source[i];
    return fx_sdk_platform(c,a,FX_BIND_CALL,tasks,FX_TASK_POST,0u,&p,sizeof(p));
}
static inline FxResult fx_sdk_task(FxSdkClient *c,const FxServiceModule *a,uint64_t tasks,uint32_t operation,uint64_t ticket) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,tasks,operation,ticket,0,0u);
}
#endif
