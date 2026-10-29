#ifndef FENESTRA_MUTEX_SDK_H
#define FENESTRA_MUTEX_SDK_H
#include "fenestra_sdk.h"
/* Original RTOS task-owned mutexes, not generation-checked resource handles.
 * Open rtos.mutex.{create,delete,lock,try-lock,lock-timeout,unlock} version 1.
 * FX_OK is transport success; interpret reply.value[0] as signed int32_t.
 * Acquisition and release MUST execute on the same original RTOS task.
 * Nested mutexes MUST be unlocked in reverse acquisition order. */
typedef struct {uint32_t attributes,priority_ceiling;} FxMutexConfig;
_Static_assert(sizeof(FxMutexConfig)==8u,"native mutex configuration ABI");
/* Native attributes 0..3. Ceiling is used by policy 3 and is 1..256.
 * Copy into an exported core.memory buffer. create copies the configuration,
 * so that buffer can be reclaimed after the native call returns. */
static inline FxResult fx_sdk_mutex_config(FxSdkClient *c,const FxServiceModule *a,
    uint64_t memory,uint64_t buffer,const FxMutexConfig *config) {
    struct {FxMemoryRequest request;FxMutexConfig config;} payload={
        .request={.handle=buffer,.bytes=sizeof(FxMutexConfig)}};
    if(!config)return FX_ARGUMENT;
    payload.config=*config;
    return fx_sdk_platform(c,a,FX_BIND_CALL,memory,FX_MEMORY_WRITE,0u,&payload,
        (uint32_t)(sizeof(FxMemoryRequest)+sizeof(FxMutexConfig)));
}
static inline FxResult fx_sdk_mutex_create(FxSdkClient *c,const FxServiceModule *a,
    uint64_t binding,uint64_t native_config_address) {
    uint64_t args[8]={native_config_address};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));
}
/* lock-timeout: 0 polls, -1 waits indefinitely, >0 uses native RTOS units.
 * Other operations ignore timeout. Do not block the original UI task.
 * Raw IDs can be reused after deletion. Coordinate all users before delete;
 * never force-delete/unlock during Panic or owner teardown. */
static inline FxResult fx_sdk_mutex_call(FxSdkClient *c,const FxServiceModule *a,
    uint64_t binding,int32_t native_id,int32_t timeout) {
    uint64_t args[8]={(uint32_t)native_id,(uint32_t)timeout};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));
}
#endif
