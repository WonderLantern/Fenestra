/* Execute a copied native call on the original UI task, and publish an input page only after completion. TPIDR is a per-CPU
 * context pointer: two different tasks can legitimately observe the same value. */
#include "fenestra_tasks_sdk.h"
#include "fenestra_native_context.h"
static FxSdkClient client;
static uint32_t stage;
static uint64_t context,tasks,ticket,worker;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || (reply.result!=FX_OK && !(stage==11u && reply.result==FX_EMPTY)))return 100+(int)stage;
        if(stage==1u)context=reply.value[0];
        if(stage==2u){worker=reply.value[0];if(!worker)return 2;}
        if(stage==3u)tasks=reply.value[0];
        if(stage==4u)ticket=reply.value[0];
        if(stage==5u) {
            if(reply.bytes!=sizeof(FxTaskStatus))return 3;
            const FxTaskStatus *s=(const void *)reply.data;
            if(s->state==FX_TASK_DONE){if(s->result!=FX_OK || !s->task || !s->value[0])return 4;++stage;}
        } else if(stage==6u){if(reply.value[1]!=FX_OK || !reply.value[2])return 5;++stage;}
        else if(stage==11u) {
            if(reply.result==FX_OK) {
                if(reply.bytes!=sizeof(FxUiEvent))return 6;
                const FxUiEvent *e=(const void *)reply.data;
                if(e->control==2u && e->event==FX_INPUT_ACTIVATE)++stage;
            }
        } else ++stage;
    }
    FxResult r;uint64_t args[8]={0};args[0]=FX_CONTEXT_TPIDR_EL1;
    if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    else if(stage==1u)r=fx_sdk_open(&client,api,"core.context",1u);
    else if(stage==2u)r=fx_sdk_platform(&client,api,FX_BIND_CALL,context,0u,0u,args,sizeof(args));
    else if(stage==3u)r=fx_sdk_open(&client,api,"core.tasks",1u);
    else if(stage==4u)r=fx_sdk_task_post(&client,api,tasks,context,args,sizeof(args));
    else if(stage==5u)r=fx_sdk_task(&client,api,tasks,FX_TASK_STATUS,ticket);
    else if(stage==6u)r=fx_sdk_task(&client,api,tasks,FX_TASK_FETCH,ticket);
    else if(stage==7u || stage==8u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,stage==7u?tasks:context,0u,0u,0,0);
    else if(stage==9u || stage==10u) {
        FxUiControl c={.abi=1u,.bytes=sizeof(c),.id=stage==9u?1u:2u,.parent=stage==10u?1u:0u,
            .kind=stage==9u?FX_UI_PAGE:FX_UI_ACTION,.flags=FX_UI_ENABLED|FX_UI_VISIBLE|(stage==9u?FX_UI_ROOT:0u)};
        const char *label=stage==9u?"Queued native call completed":"Finish task test";
        for(uint32_t i=0;label[i];++i)c.label[i]=label[i];r=fx_sdk_ui(&client,api,&c);
    } else if(stage==11u)r=fx_sdk_events(&client,api);
    else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    return r==FX_OK?0:7;
}
