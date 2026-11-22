/* Ordinary APP: open/close the same interface declared at package build time.
 * No firmware addresses, native calls, hardware action or module catalog. */
#include "fenestra_sdk.h"
static FxSdkClient client;
static uint64_t binding;
static uint32_t stage;
enum {APP,OPEN,CLOSE,FINISH,DONE};
static FxResult submit(const FxServiceModule *api) {
    switch(stage) {
        case APP:return fx_sdk_lifecycle(&client,api,FX_LIFE_CONFIGURE,FX_MODULE_APP);
        case OPEN:return fx_sdk_open_instance(&client,api,"core.schedule",1u);
        case CLOSE:return fx_sdk_platform(&client,api,FX_BIND_CLOSE,binding,0u,0u,0,0u);
        case FINISH:return fx_sdk_lifecycle(&client,api,FX_LIFE_FINISH,0u);
        default:return FX_OK;
    }
}
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(api->service_abi!=FX_SERVICE_ABI)return 2;
    if(client.pending) {
        FxServiceReply reply;FxResult result=fx_sdk_take(&client,api,&reply);
        if(result==FX_BUSY)return 0;
        if(result!=FX_OK)return 3;
        if(reply.result==FX_BUSY)return submit(api)==FX_OK?0:4;
        if(stage==OPEN) {
            if(reply.result==FX_OK){binding=reply.value[0];stage=CLOSE;}
            else stage=FINISH; /* Runtime state still requires normal error handling. */
        } else if(reply.result!=FX_OK)return 5;
        else if(stage==APP)stage=OPEN;
        else if(stage==CLOSE)stage=FINISH;
        else if(stage==FINISH)stage=DONE;
    }
    return submit(api)==FX_OK?0:6;
}
