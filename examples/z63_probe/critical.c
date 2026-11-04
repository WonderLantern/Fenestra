/* A module-owned native lock brackets one context read and is reclaimed only
 * after unlock. This private lock makes no claim about stock-code exclusion. */
#include "fenestra_critical_sdk.h"
#include "fenestra_native_context.h"
static FxSdkClient client;
static uint32_t stage;
static uint64_t memory,context,critical,buffer,address,before;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply a;FxResult r=fx_sdk_take(&client,api,&a);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || a.result!=FX_OK)return 100+(int)stage;
        if(stage==1u)memory=a.value[0];
        if(stage==2u)context=a.value[0];
        if(stage==3u)critical=a.value[0];
        if(stage==4u){buffer=a.value[0];address=a.value[1];if(!address)return 2;}
        if(stage==6u)before=a.value[0];
        if(stage==7u && (!(a.value[0]&0x80u) || a.value[2] || a.value[3]))return 3;
        if(stage==8u && a.value[0]!=before)return 4;
        if(stage==9u) {
            if(a.bytes!=16u || a.data[0]!=255u)return 5;
            for(uint32_t i=1u;i<8u;++i)if(a.data[i])return 6;
        }
        ++stage;
    }
    FxResult r;uint64_t args[8]={FX_CONTEXT_DAIF};
    if(stage==0u)r=fx_sdk_lifecycle(&client,api,FX_LIFE_CONFIGURE,FX_MODULE_APP);
    else if(stage==1u)r=fx_sdk_open(&client,api,"core.memory",1u);
    else if(stage==2u)r=fx_sdk_open(&client,api,"core.context",1u);
    else if(stage==3u)r=fx_sdk_open(&client,api,"core.critical",1u);
    else if(stage==4u)r=fx_sdk_memory_allocate(&client,api,memory,16u,1u);
    else if(stage==5u) {
        struct {FxMemoryRequest q;uint8_t lock[16];} write={.q={buffer,16u,0u},.lock={255u}};
        r=fx_sdk_platform(&client,api,FX_BIND_CALL,memory,FX_MEMORY_WRITE,0u,&write,sizeof(write));
    } else if(stage==6u || stage==8u)r=fx_sdk_platform(&client,api,FX_BIND_CALL,context,0u,0u,args,sizeof(args));
    else if(stage==7u)r=fx_sdk_critical_call(&client,api,critical,address,context,args,sizeof(args));
    else if(stage==9u || stage==10u) {
        FxMemoryRequest q={buffer,stage==9u?16u:0u,0u};
        r=fx_sdk_platform(&client,api,FX_BIND_CALL,memory,stage==9u?FX_MEMORY_READ:FX_MEMORY_RECLAIM,0u,&q,sizeof(q));
    } else if(stage>=11u && stage<=13u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,stage==11u?critical:stage==12u?context:memory,0u,0u,0,0);
    else r=fx_sdk_lifecycle(&client,api,FX_LIFE_FINISH,0u);
    return r==FX_OK?0:7;
}
