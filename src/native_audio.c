#include "fenestra_audio_output.h"
#include "fenestra_memory.h"

static FxResult read_module(void *context,uint64_t address,void *out,uint32_t bytes) {
    return fx_platform_module_transfer(context,address,out,bytes,0u);
}
FxResult fx_audio_resource(void *context,uint32_t operation,const FxNamedBinding *binding,FxPlatformOwner *owner,
                           uint64_t token,uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)binding;(void)token;
    if(!owner || !cookie)return FX_ARGUMENT;
    if(operation==FX_RESOURCE_ATTACH){*cookie=0u;return FX_OK;}
    FxAudioStream *stream=(FxAudioStream *)(uintptr_t)*cookie;
    FxAudioOutput *output=fx_audio_output_current();
    if(stream && !fx_audio_stream_owner(stream,owner->owner,owner->generation))return FX_STALE;
    if(operation==FX_RESOURCE_DETACH || operation==FX_RESOURCE_PANIC) {
        if(!stream)return FX_OK;
        FxResult result=fx_audio_stream_close(output,stream);
        if(result==FX_OK)*cookie=0u;
        return result;
    }
    if(operation!=FX_RESOURCE_CALL || !q || !a)return FX_ARGUMENT;
    if(q->arg[2]>FX_AUDIO_STOP || (q->arg[2]!=FX_AUDIO_GAIN && q->arg[3]))return FX_ARGUMENT;
    if(q->arg[2]==FX_AUDIO_QUERY) {
        if(q->bytes)return FX_ARGUMENT;
        FxAudioInfo info;FxResult result=fx_audio_info(output,&info);
        if(result==FX_OK){memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);}
        return result;
    }
    if(q->arg[2]==FX_AUDIO_CONFIGURE) {
        if(q->bytes!=sizeof(FxAudioConfig))return FX_ARGUMENT;
        if(stream)return FX_STATE;
        FxAudioConfig config;memcpy(&config,q->data,sizeof(config));
        FxResult result=fx_audio_stream_create(output,owner->owner,owner->generation,&config,&stream);
        if(result==FX_OK)*cookie=(uintptr_t)stream;
        return result;
    }
    if(!output)return FX_CAPABILITY;
    if(!stream)return FX_STATE;
    if(q->arg[2]==FX_AUDIO_WRITE) {
        if(q->bytes!=sizeof(FxAudioWrite))return FX_ARGUMENT;
        FxAudioWrite write;memcpy(&write,q->data,sizeof(write));
        if(write.reserved)return FX_ARGUMENT;
        return fx_audio_stream_write(output,stream,write.samples,write.frames,read_module,owner);
    }
    if(q->bytes)return FX_ARGUMENT;
    if(q->arg[2]==FX_AUDIO_STATUS) {
        FxAudioStatus status;FxResult result=fx_audio_stream_status(output,stream,&status);
        if(result==FX_OK){memcpy(a->data,&status,sizeof(status));a->bytes=sizeof(status);}
        return result;
    }
    if(q->arg[3]>UINT32_MAX)return FX_ARGUMENT;
    return fx_audio_stream_control(output,stream,(uint32_t)q->arg[2],(uint32_t)q->arg[3]);
}
