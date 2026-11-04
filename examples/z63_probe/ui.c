#include "fenestra_sdk.h"
static FxSdkClient client;
static uint32_t stage;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || (reply.result!=FX_OK && !(stage==3u && reply.result==FX_EMPTY)))return 100+(int)stage;
        if(stage==3u && reply.result==FX_OK) {
            if(reply.bytes!=sizeof(FxUiEvent))return 2;
            const FxUiEvent *e=(const void *)reply.data;
            if(e->control==2u && e->event==FX_INPUT_ACTIVATE)stage=4u;
        } else if(stage<3u)++stage;
    }
    FxResult r;uint64_t args[4]={0};
    if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    else if(stage==1u || stage==2u) {
        FxUiControl c={.abi=1u,.bytes=sizeof(c),.id=stage,.parent=stage==2u?1u:0u,
            .kind=stage==1u?FX_UI_PAGE:FX_UI_ACTION,.flags=FX_UI_ENABLED|FX_UI_VISIBLE|(stage==1u?FX_UI_ROOT:0u)};
        const char *label=stage==1u?"Z6III SDK application":"Finish application";
        for(uint32_t i=0;label[i];++i)c.label[i]=label[i];r=fx_sdk_ui(&client,api,&c);
    } else if(stage==3u)r=fx_sdk_events(&client,api);
    else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    return r==FX_OK?0:3;
}
