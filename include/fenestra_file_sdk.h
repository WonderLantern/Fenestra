#ifndef FENESTRA_FILE_SDK_H
#define FENESTRA_FILE_SDK_H
#include "fenestra_sdk.h"
/* card.files owns per-module streams, retained on uncertain close/Panic.
 * This adapter submits copied requests; consume the reply before reuse. */
static inline FxResult fx_sdk_file_seek(FxSdkClient *c,const FxServiceModule *a,
    uint64_t binding,uint64_t handle,int64_t offset,uint32_t whence) {
    if(whence>FX_SEEK_END)return FX_ARGUMENT;
    FxFileSeekRequest request={handle,offset,whence,0u};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_FILE_SEEK,0u,&request,sizeof(request));
}
static inline FxResult fx_sdk_file_tell(FxSdkClient *c,const FxServiceModule *a,
    uint64_t binding,uint64_t handle) {
    return fx_sdk_file_seek(c,a,binding,handle,0,FX_SEEK_CURRENT);
}
#endif
