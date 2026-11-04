/* Allow peer publication before a deliberate data abort. */
#include "fenestra_sdk.h"
static FxSdkClient client;
static uint32_t count;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply a;FxResult r=fx_sdk_take(&client,api,&a);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || a.result!=FX_OK)return 2;
        ++count;
    }
    if(count>=10u){*(volatile uint32_t *)(uintptr_t)0u=1u;return 3;}
    return fx_sdk_submit(&client,api,FX_S_QUERY,0,0,0)==FX_OK?0:4;
}
