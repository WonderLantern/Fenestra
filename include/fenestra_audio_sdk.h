#ifndef FENESTRA_AUDIO_SDK_H
#define FENESTRA_AUDIO_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_audio.h"

static inline FxResult fx_sdk_audio_query(FxSdkClient *c,const FxServiceModule *a,uint64_t binding) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_AUDIO_QUERY,0u,0,0u);
}
static inline FxResult fx_sdk_audio_configure(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
                                             const FxAudioConfig *config) {
    if(!config)return FX_ARGUMENT;
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_AUDIO_CONFIGURE,0u,config,sizeof(*config));
}
static inline FxResult fx_sdk_audio_write(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
                                         const void *samples,uint32_t frames) {
    if(!samples || !frames || frames>FX_AUDIO_MAX_SUBMIT_FRAMES)return FX_ARGUMENT;
    FxAudioWrite request={(uintptr_t)samples,frames,0u};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_AUDIO_WRITE,0u,&request,sizeof(request));
}
/* START/PAUSE/STATUS/DRAIN/STOP use value=0. GAIN uses unsigned Q16 [0,65536].
 * Close with FX_BIND_CLOSE after DRAIN completes, or discard with STOP first. */
static inline FxResult fx_sdk_audio_control(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
                                            uint32_t operation,uint32_t value) {
    if(operation<FX_AUDIO_START || operation>FX_AUDIO_STOP ||
       (operation==FX_AUDIO_GAIN?value>FX_AUDIO_GAIN_ONE:value!=0u))return FX_ARGUMENT;
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,operation,value,0,0u);
}
#endif
