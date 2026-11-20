/* Native shooting overlay; retries only pre-publication STATE while menus own
 * the view, then checks three copied frames and retains presentation briefly. */
#include "fenestra_scene_sdk.h"
static FxSdkClient client;
static uint32_t stage,frames;
static uint64_t scene,schedule,ticket;
static FxSceneCommand commands[5];
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);if(r==FX_BUSY)return 0;
        if(r!=FX_OK)return 2;
        if(stage==3u && reply.result==FX_STATE)return 0;
        if(reply.result!=FX_OK)return 100+(int)stage;
        if(stage==1u)scene=reply.value[0];
        if(stage==2u)schedule=reply.value[0];
        if(stage==3u){if(reply.value[0]!=++frames)return 3;}
        if(stage==4u){ticket=reply.value[0];}
        if(stage==5u && frames<3u){stage=3u;return 0;}
        ++stage;
    }
    FxResult r;uint64_t args[4]={0};
    if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    else if(stage==1u)r=fx_sdk_open(&client,api,"camera.display.scene",1u);
    else if(stage==2u)r=fx_sdk_open(&client,api,"core.schedule",1u);
    else if(stage==3u){commands[0]=fx_scene_clear(0x202030u);
        commands[1]=fx_scene_rectangle(20,20,(uint16_t)(180u+frames*10u),40,0x00ff00u);commands[2]=fx_scene_glyph(240,25,'Z',4,0xffffffu);commands[3]=fx_scene_glyph(270,25,'6',4,0xffffffu);commands[4]=fx_scene_glyph(300,25,'3',4,0xffffffu);r=fx_sdk_scene_submit_global(&client,api,scene,720,480,commands,5);}
    else if(stage==4u || stage==5u)r=fx_sdk_wait(&client,api,schedule,ticket,stage==4u?0u:frames<3u?500u:5000u);
    else if(stage==6u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,scene,0,0,0,0);
    else if(stage==7u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,schedule,0,0,0,0);
    else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    return r==FX_OK?0:4;
}
