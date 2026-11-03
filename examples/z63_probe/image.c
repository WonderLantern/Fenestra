#include "fenestra_scene_sdk.h"
static FxSdkClient client;
static uint32_t stage,frames;
static uint64_t scene;
static FxSceneCommand commands[6];
static struct {FxSceneImage image;uint8_t pixels[8u*8u*4u];} texture;
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || (reply.result!=FX_OK && !((stage==3u || stage==6u) && reply.result==FX_EMPTY)))return 100+(int)stage;
        if((stage==3u || stage==6u) && reply.result==FX_OK) {
            if(reply.bytes!=sizeof(FxUiEvent))return 2;
            const FxUiEvent *e=(const void *)reply.data;
            if(stage==3u && e->control==1u && e->event==FX_INPUT_FOCUS)stage=4u;
            else if(stage==6u && e->control==2u && e->event==FX_INPUT_ACTIVATE)stage=7u;
        } else if(stage==4u){scene=reply.value[0];stage=5u;}
        else if(stage==5u){++frames;if(frames==3u)stage=6u;}
        else if(stage==7u)stage=8u;
        else if(stage<3u)++stage;
    }
    FxResult r;uint64_t args[4]={0};
    if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    else if(stage==1u || stage==2u) {
        FxUiControl c={.abi=1u,.bytes=sizeof(c),.id=stage,.parent=stage==2u?1u:0u,
            .kind=stage==1u?FX_UI_PAGE:FX_UI_ACTION,.flags=FX_UI_ENABLED|FX_UI_VISIBLE|(stage==1u?FX_UI_ROOT:0u)};
        const char *label=stage==1u?"Z6III SDK image":"Finish application";
        for(uint32_t i=0;label[i];++i)c.label[i]=label[i];r=fx_sdk_ui(&client,api,&c);
    } else if(stage==3u || stage==6u)r=fx_sdk_events(&client,api);
    else if(stage==4u)r=fx_sdk_open(&client,api,"camera.display.scene",1u);
    else if(stage==5u) {
        commands[0]=fx_scene_clear(0x202030u);
        commands[1]=fx_scene_rectangle(20u,220u,(uint16_t)(200u+frames*30u),70u,0x00ff00u);
        commands[2]=fx_scene_glyph(300u,225u,'Z',4u,0xffffffu);
        commands[3]=fx_scene_glyph(330u,225u,'6',4u,0xffffffu);
        commands[4]=fx_scene_glyph(360u,225u,'3',4u,0xffffffu);
        texture.image=(FxSceneImage){8u,8u,32u,FX_SCENE_RGBA8,sizeof(FxSceneImage),0u};
        for(uint32_t y=0;y<8u;++y)for(uint32_t x=0;x<8u;++x) {
            uint32_t at=(y*8u+x)*4u;
            texture.pixels[at]=(uint8_t)((y<4u?x<4u:x>=4u)?255u:0u);
            texture.pixels[at+1u]=(uint8_t)(x>=4u?255u:0u);
            texture.pixels[at+2u]=(uint8_t)(y>=4u?255u:0u);
            texture.pixels[at+3u]=255u;
        }
        commands[5]=fx_scene_image(420u,220u,96u,96u,0u);
        r=fx_sdk_scene_submit_images(&client,api,scene,1u,720u,480u,commands,6u,&texture,sizeof(texture));
    } else if(stage==7u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,scene,0u,0u,0,0);
    else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
    return r==FX_OK?0:3;
}
