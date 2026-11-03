/* Real native event flags, caller context, translation and resource snapshots.
 * Each operation travels through the copied SDK exchange from EL0. */
#include "fenestra_events_sdk.h"
#include "fenestra_native_context.h"
#include "fenestra_accounting.h"
static FxSdkClient client;
static uint32_t stage;
static uint64_t binding,event;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);
        if(r==FX_BUSY)return 0;
        if(r!=FX_OK || reply.result!=(stage==6u?FX_EMPTY:FX_OK))return 100+(int)stage;
        if(stage==1u || stage==11u || stage==14u || stage==17u)binding=reply.value[0];
        if(stage==2u){event=reply.value[0];if(!event)return 2;}
        if((stage==3u && reply.value[0]!=3u) || (stage==5u && reply.value[0]!=2u) ||
           (stage==8u && reply.value[0]!=8u) || (stage==12u && reply.value[0]!=4u))return 3;
        if(stage==15u && (reply.value[0]&1u))return 4;
        if(stage==18u) {
            if(reply.bytes!=sizeof(FxAccountInfo))return 5;
            const FxAccountInfo *info=(const FxAccountInfo *)(const void *)reply.data;
            if(info->abi!=1u || info->owner!=api->owner || info->generation!=api->generation)return 6;
        }
        ++stage;
    }
    FxResult r;uint64_t args[8]={0};
    if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    else if(stage==1u)r=fx_sdk_open(&client,api,"rtos.events",1u);
    else if(stage==2u)r=fx_sdk_event(&client,api,binding,FX_EVENT_CREATE,0u,3u,0u);
    else if(stage==3u)r=fx_sdk_event(&client,api,binding,FX_EVENT_POLL,event,1u,FX_EVENT_ANY);
    else if(stage==4u)r=fx_sdk_event(&client,api,binding,FX_EVENT_CLEAR,event,1u,0u);
    else if(stage==5u)r=fx_sdk_event(&client,api,binding,FX_EVENT_POLL,event,2u,FX_EVENT_ALL|FX_EVENT_CLEAR_MATCH);
    else if(stage==6u)r=fx_sdk_event(&client,api,binding,FX_EVENT_POLL,event,1u,FX_EVENT_ANY);
    else if(stage==7u)r=fx_sdk_event(&client,api,binding,FX_EVENT_SET,event,8u,0u);
    else if(stage==8u)r=fx_sdk_event(&client,api,binding,FX_EVENT_POLL,event,8u,FX_EVENT_ANY|FX_EVENT_CLEAR_ALL);
    else if(stage==9u)r=fx_sdk_event(&client,api,binding,FX_EVENT_RELEASE,event,0u,0u);
    else if(stage==10u || stage==13u || stage==16u || stage==19u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,binding,0u,0u,0,0);
    else if(stage==11u)r=fx_sdk_open(&client,api,"core.context",1u);
    else if(stage==12u){args[0]=FX_CONTEXT_CURRENT_EL;r=fx_sdk_platform(&client,api,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));}
    else if(stage==14u)r=fx_sdk_open(&client,api,"core.translation",1u);
    else if(stage==15u){args[0]=0x40160000u;r=fx_sdk_platform(&client,api,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));}
    else if(stage==17u)r=fx_sdk_open(&client,api,"core.resources",1u);
    else if(stage==18u)r=fx_sdk_platform(&client,api,FX_BIND_CALL,binding,FX_ACCOUNT_QUERY,0u,0,0);
    else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    return r==FX_OK?0:7;
}
