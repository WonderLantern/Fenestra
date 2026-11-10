/* A live peer must retire through native stop when another owner panics. */
#include "fenestra_sdk.h"
static FxSdkClient client;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply a;FxResult r=fx_sdk_take(&client,api,&a);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || a.result!=FX_OK)return 2;
    }
    return fx_sdk_submit(&client,api,FX_S_QUERY,0,0,0)==FX_OK?0:3;
}
