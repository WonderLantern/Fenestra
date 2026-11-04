/* Copied SDK query/translation against original C2.01 native page tables.
 * This control does not reserve a VA or mutate a mapping. */
#include "fenestra_mapping_sdk.h"
static FxSdkClient client;
static uint32_t stage;
static uint64_t binding;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);
        if(r==FX_BUSY)return 0;
        if(r!=FX_OK || reply.result!=FX_OK)return 100+(int)stage;
        if(stage==1u)binding=reply.value[0];
        if(stage==2u) {
            if(reply.bytes!=sizeof(FxMappingInfo))return 2;
            const FxMappingInfo *info=(const void *)reply.data;
            if(info->abi!=1u || info->page_bytes!=4096u || !(info->operations&(1u<<FX_MAPPING_TRANSLATE)))return 3;
        }
        if(stage==3u && reply.value[0]!=0x40160000u)return 4;
        ++stage;
    }
    FxResult r;uint64_t args[8]={0};
    if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    else if(stage==1u)r=fx_sdk_open(&client,api,"rtos.mappings",1u);
    else if(stage==2u)r=fx_sdk_mapping(&client,api,binding,FX_MAPPING_QUERY,0);
    else if(stage==3u){FxMappingRequest q={.address=0x40160000u};r=fx_sdk_mapping(&client,api,binding,FX_MAPPING_TRANSLATE,&q);}
    else if(stage==4u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,binding,0u,0u,0,0);
    else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    return r==FX_OK?0:5;
}
