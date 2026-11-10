/* Full-system copied-message control: platform identity and native heap
 * provider allocation/write/read/release, followed by an orderly APP finish. */
#include "fenestra_sdk.h"
static FxSdkClient client;
static uint32_t stage;
static uint64_t binding,buffer;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);
        if(r==FX_BUSY)return 0;
        if(r!=FX_OK || reply.result!=FX_OK)return 100+(int)stage;
        if(stage==2u) {
            if(reply.bytes!=sizeof(FxPlatformInfo))return 2;
            const FxPlatformInfo *p=(const FxPlatformInfo *)(const void *)reply.data;
            if(p->abi!=1u || p->model[0]!='N' || p->firmware[2]!='2')return 3;
        }
        if(stage==3u)binding=reply.value[0];
        if(stage==4u){buffer=reply.value[0];if(!buffer || reply.value[1])return 4;}
        if(stage==6u && (reply.bytes!=4u || reply.data[0]!=0x12u || reply.data[3]!=0x78u))return 5;
        ++stage;
    }
    uint64_t args[4]={0};FxResult r;
    if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    else if(stage==1u)r=fx_sdk_submit(&client,api,FX_S_QUERY,0,0,0);
    else if(stage==2u)r=fx_sdk_platform(&client,api,FX_BIND_INFO,0u,0u,0u,0,0);
    else if(stage==3u)r=fx_sdk_open(&client,api,"core.memory",1u);
    else if(stage==4u)r=fx_sdk_memory_allocate(&client,api,binding,64u,0u);
    else if(stage==5u) {
        struct {FxMemoryRequest request;uint8_t bytes[4];} write={{buffer,4u,0u},{0x12u,0x34u,0x56u,0x78u}};
        r=fx_sdk_platform(&client,api,FX_BIND_CALL,binding,FX_MEMORY_WRITE,0u,&write,sizeof(FxMemoryRequest)+4u);
    } else if(stage==6u) {
        FxMemoryRequest read={buffer,4u,0u};r=fx_sdk_platform(&client,api,FX_BIND_CALL,binding,FX_MEMORY_READ,0u,&read,sizeof(read));
    } else if(stage==7u)r=fx_sdk_memory_release(&client,api,binding,buffer,0u);
    else if(stage==8u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,binding,0u,0u,0,0);
    else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    return r==FX_OK?0:6;
}
