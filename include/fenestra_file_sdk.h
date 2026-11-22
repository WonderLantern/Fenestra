#ifndef FENESTRA_FILE_SDK_H
#define FENESTRA_FILE_SDK_H
#include "fenestra_sdk.h"
static inline FxResult fx_sdk_file_module(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint64_t file,void *buffer,uint32_t bytes,uint32_t writing) {
    if(!buffer || !bytes || writing>1u)return FX_ARGUMENT;
    FxFileModuleRequest request={file,(uintptr_t)buffer,bytes,0u};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,writing?FX_FILE_WRITE_MODULE:FX_FILE_READ_MODULE,
        0u,&request,sizeof(request));
}
static inline FxResult fx_sdk_file_rename(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    const char *from,const char *to) {
    if(!from || !to)return FX_ARGUMENT;
    FxFileRename request={0};uint32_t n=0u;
    while(n<64u && from[n]){request.from[n]=from[n];++n;}
    if(!n || n==64u)return FX_ARGUMENT;
    n=0u;while(n<64u && to[n]){request.to[n]=to[n];++n;}
    if(!n || n==64u)return FX_ARGUMENT;
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_FILE_RENAME,0u,&request,sizeof(request));
}
static inline FxResult fx_sdk_file_original_open(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    const char *name,uint32_t flags,uint32_t attributes) {
    if(!name)return FX_ARGUMENT;
    FxFileOriginalOpen request={.file={.mode=FX_FILE_ORIGINAL_FLAGS},.flags=flags,.attributes=attributes};uint32_t n=0u;
    while(n<64u && name[n]){request.file.name[n]=name[n];++n;}
    if(!n || n==64u)return FX_ARGUMENT;
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_FILE_OPEN,0u,&request,sizeof(request));
}
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
