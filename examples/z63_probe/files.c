#include "fenestra_file_sdk.h"
static FxResult file_call(FxSdkClient *c,const FxServiceModule *api,uint64_t binding,uint32_t op,uint64_t handle,const char *name,const void *data,uint32_t bytes,uint32_t mode) {
    struct {FxFileRequest request;uint8_t data[513];} q={.request={.handle=handle,.bytes=bytes,.mode=mode}};
    if(name)for(uint32_t i=0;name[i] && i<sizeof(q.request.name)-1u;++i)q.request.name[i]=name[i];
    if(data)for(uint32_t i=0;i<bytes;++i)q.data[i]=((const uint8_t *)data)[i];
    return fx_sdk_platform(c,api,FX_BIND_CALL,binding,op,0u,&q,(uint32_t)sizeof(q.request)+(data?bytes:0u));
}
static FxSdkClient client;
static uint32_t stage;
static uint64_t binding,file;
static uint8_t bytes[513];
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || reply.result!=(stage==9u?FX_IO:FX_OK))return 100+(int)stage;
        if(stage==1u)binding=reply.value[0];
        if(stage==2u || stage==5u)file=reply.value[0];
        if(stage==3u && reply.value[0]!=sizeof(bytes))return 2;
        if(stage==6u && reply.value[0]!=255u)return 3;
        if(stage==7u) {
            if(reply.bytes!=8u)return 4;
            for(uint32_t i=0;i<8u;++i)if(reply.data[i]!=(uint8_t)(255u+i))return 5;
        }
        ++stage;
    }
    FxResult r;uint64_t args[4]={0};
    if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    else if(stage==1u)r=fx_sdk_open(&client,api,"card.files",1u);
    else if(stage==2u || stage==5u || stage==9u)r=file_call(&client,api,binding,FX_FILE_OPEN,0u,"sdk63.dat",0,0u,stage==5u?FX_FILE_READ_ONLY:FX_FILE_CREATE_EXCLUSIVE);
    else if(stage==3u){for(uint32_t i=0;i<sizeof(bytes);++i)bytes[i]=(uint8_t)i;r=file_call(&client,api,binding,FX_FILE_WRITE,file,0,bytes,sizeof(bytes),0u);}
    else if(stage==4u || stage==8u)r=file_call(&client,api,binding,FX_FILE_CLOSE,file,0,0,0u,0u);
    else if(stage==6u)r=fx_sdk_file_seek(&client,api,binding,file,255,FX_SEEK_SET);
    else if(stage==7u)r=file_call(&client,api,binding,FX_FILE_READ,file,0,0,8u,0u);
    else if(stage==10u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,binding,0u,0u,0,0);
    else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    return r==FX_OK?0:6;
}
