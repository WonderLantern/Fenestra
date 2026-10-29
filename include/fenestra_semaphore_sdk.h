#ifndef FENESTRA_SEMAPHORE_SDK_H
#define FENESTRA_SEMAPHORE_SDK_H
#include "fenestra_sdk.h"
/* Native RTOS IDs, NOT generation-checked Fenestra resource handles. The
 * creator coordinates all users before delete; Panic never force-deletes.
 * Open rtos.semaphore.{create,delete,signal,signal-irq,wait,poll,wait-timeout}.
 * FX_OK is transport success; the signed native result is reply.value[0]. */
typedef struct {uint32_t attributes,initial_count,maximum_count;} FxSemaphoreConfig;
_Static_assert(sizeof(FxSemaphoreConfig)==12u,"native semaphore configuration ABI");
/* Copy the configuration into an exported core.memory buffer before create.
 * The original create copies it, so storage can be reclaimed after return. */
static inline FxResult fx_sdk_semaphore_config(FxSdkClient *c,const FxServiceModule *a,
    uint64_t memory,uint64_t buffer,const FxSemaphoreConfig *config) {
    struct {FxMemoryRequest request;FxSemaphoreConfig config;} payload={
        .request={.handle=buffer,.bytes=sizeof(FxSemaphoreConfig)}};
    if(!config)return FX_ARGUMENT;
    payload.config=*config;
    return fx_sdk_platform(c,a,FX_BIND_CALL,memory,FX_MEMORY_WRITE,0u,&payload,
        (uint32_t)(sizeof(FxMemoryRequest)+sizeof(FxSemaphoreConfig)));
}
static inline FxResult fx_sdk_semaphore_create(FxSdkClient *c,const FxServiceModule *a,
    uint64_t create_binding,uint64_t native_config_address) {
    uint64_t args[8]={native_config_address};
    return fx_sdk_platform(c,a,FX_BIND_CALL,create_binding,0u,0u,args,sizeof(args));
}
/* For wait-timeout: 0 polls, -1 waits indefinitely, positive values are native
 * RTOS timeout units. Do not block the UI task or a task required to signal.
 * Other operations ignore timeout. signal-irq requires an original IRQ
 * context; an ordinary module worker must use signal instead. */
static inline FxResult fx_sdk_semaphore_call(FxSdkClient *c,const FxServiceModule *a,
    uint64_t binding,int32_t native_id,int32_t timeout) {
    uint64_t args[8]={(uint32_t)native_id,(uint32_t)timeout};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));
}
#endif
