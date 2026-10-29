#ifndef FENESTRA_VIDEO_SDK_H
#define FENESTRA_VIDEO_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_video_configuration.h"
/* All calls submit one copied exchange and return. Take its reply before the
 * next call. Keep only one mode in module storage; the adapter owns the full
 * upload. Header first, then modes in order, then COMMIT. The caller advances
 * its cursor only after the adapter acknowledges the exact next offset. */
static inline FxResult fx_sdk_video_query(FxSdkClient *c,const FxServiceModule *a,uint64_t binding) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_VIDEO_QUERY,0u,0,0u);
}
static inline FxResult fx_sdk_video_begin(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,uint32_t count) {
    uint32_t bytes=fx_video_configuration_bytes(count);if(!bytes)return FX_ARGUMENT;
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_VIDEO_BEGIN,bytes,0,0u);
}
static inline FxResult fx_sdk_video_header(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,uint32_t count) {
    uint32_t bytes=fx_video_configuration_bytes(count);if(!bytes)return FX_ARGUMENT;
    const FxVideoConfigurationData header={FX_VIDEO_CONFIGURATION_ABI,bytes,count,0u};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_VIDEO_APPEND,0u,&header,sizeof(header));
}
static inline FxResult fx_sdk_video_mode(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint32_t index,uint32_t fragment,const FxVideoConfigurationMode *mode) {
    if(!mode || index>=FX_VIDEO_MAX_MODES || fragment>=sizeof(*mode))return FX_ARGUMENT;
    uint32_t bytes=(uint32_t)sizeof(*mode)-fragment;if(bytes>FX_SERVICE_BYTES)bytes=FX_SERVICE_BYTES;
    uint32_t offset=(uint32_t)sizeof(FxVideoConfigurationData)+index*(uint32_t)sizeof(*mode)+fragment;
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_VIDEO_APPEND,offset,(const uint8_t *)mode+fragment,bytes);
}
static inline FxResult fx_sdk_video_commit(FxSdkClient *c,const FxServiceModule *a,uint64_t binding) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_VIDEO_COMMIT,0u,0,0u);
}
#endif
