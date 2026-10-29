/* Generic SDK front end. Animation policy and character storage remain in
 * this module; the core sees only camera.display.scene and ordinary UI. */
#include "fenestra_scene_sdk.h"
#define fenestra_module_entry rain_step
#include "module.c"
#undef fenestra_module_entry
enum {SDK_OPEN,SDK_QUERY,SDK_UI_QUERY,SDK_PAGE,SDK_WAIT,SDK_FRAME,SDK_FOCUS};
static FxSdkClient sdk;
static uint32_t sdk_stage,sdk_frame,frame_prepared;
static uint32_t ui_capabilities;
static uint64_t focus_epoch;
static uint64_t display_binding;
static FxSceneCommand commands[FX_DEMO_DRAW_CAPACITY];
static FxDemoScene scene={1280u,800u,FX_DEMO_DRAW_CAPACITY,0u,commands,0,0u,0u};
static FxDemoStats stats;
static FxResult prepare_frame(void) {
    FxDemoServices services={.base={.abi=1u,.bytes=sizeof(services)},.frame=sdk_frame,.stats=&stats,.scene=&scene};
    if(rain_step(&services.base))return FX_INTEGRITY;
    /* Rain owns nonoverlapping glyph cells. Quantize and bucket its colors
     * here to minimize native paths; generic rendering preserves arbitrary
     * colors and ordering for other modules. No second character container. */
    uint32_t colors[20],count=0u;
    for(uint32_t i=1u;i<scene.count;++i) {
        uint32_t rgb=commands[i].rgb;
        if(rgb!=0xd5ffe6u && rgb!=0xc1ffe5u && rgb!=0x54af88u && rgb!=0x3e8665u) {
            uint32_t level=(((rgb>>8)&255u)+7u)&~15u;if(level>240u)level=240u;
            rgb=(level/14u)<<16|level<<8|level/3u;
        }
        commands[i].rgb=rgb;uint32_t j=0u;while(j<count && colors[j]!=rgb)++j;
        if(j==count){if(count==20u)return FX_BOUNDS;colors[count++]=rgb;}
    }
    uint32_t at=1u;
    for(uint32_t color=0u;color<count;++color)
        for(uint32_t i=at;i<scene.count;++i)if(commands[i].rgb==colors[color]) {
            FxSceneCommand saved=commands[at];commands[at++]=commands[i];commands[i]=saved;
        }
    return FX_OK;
}
static void input(const FxServiceReply *r) {
    if(ui_capabilities&FX_UI_CAP_FOCUS)return;
    if(r->bytes!=sizeof(FxUiEvent))return;
    FxUiEvent e;for(uint32_t i=0;i<sizeof(e);++i)((uint8_t *)&e)[i]=r->data[i];
    if(e.control==1u && e.event==FX_INPUT_FOCUS){sdk_frame=0u;frame_prepared=0u;sdk_stage=SDK_FRAME;}
    if(e.control==1u && e.event==FX_INPUT_BLUR)sdk_stage=SDK_WAIT;
}
static int focused(const FxServiceReply *r) {
    FxUiFocus focus;if(r->bytes!=sizeof(focus))return 0;
    for(uint32_t i=0;i<sizeof(focus);++i)((uint8_t *)&focus)[i]=r->data[i];
    if(focus.abi!=1u || focus.bytes!=sizeof(focus) || focus.foreground>1u)return 0;
    if(focus.foreground && focus.control==1u) {
        if(focus.epoch!=focus_epoch){sdk_frame=0u;frame_prepared=0u;focus_epoch=focus.epoch;}
        sdk_stage=SDK_FRAME;
    } else sdk_stage=SDK_WAIT;
    return 1;
}
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(api->service_abi!=FX_SERVICE_ABI || !api->exchange)return 2;
    if(sdk.pending) {
        FxServiceReply r;if(fx_sdk_take(&sdk,api,&r)!=FX_OK)return 3;
        if(r.result==FX_BUSY) { /* Retry the same operation/frame. */ }
        else if(sdk_stage==SDK_UI_QUERY && (r.result==FX_ARGUMENT || r.result==FX_CAPABILITY))sdk_stage=SDK_PAGE;
        else if(sdk_stage==SDK_WAIT && (r.result==FX_OK || r.result==FX_EMPTY)) {
            input(&r);if(ui_capabilities&FX_UI_CAP_FOCUS)sdk_stage=SDK_FOCUS;
        }
        else if(sdk_stage==SDK_FRAME && r.result==FX_STATE)sdk_stage=SDK_WAIT;
        else {
            if(r.result!=FX_OK)return 4;
            if(sdk_stage==SDK_OPEN){display_binding=r.value[0];sdk_stage=SDK_QUERY;}
            else if(sdk_stage==SDK_QUERY) {
                FxSceneInfo info;if(r.bytes!=sizeof(info))return 5;
                for(uint32_t i=0;i<sizeof(info);++i)((uint8_t *)&info)[i]=r.data[i];
                if(info.abi!=FX_SCENE_ABI || info.max_commands<FX_DEMO_DRAW_CAPACITY || info.format!=1u)return 6;
                sdk_stage=SDK_UI_QUERY;
            } else if(sdk_stage==SDK_UI_QUERY) {
                FxUiInfo info;if(r.bytes!=sizeof(info))return 9;
                for(uint32_t i=0;i<sizeof(info);++i)((uint8_t *)&info)[i]=r.data[i];
                if(info.abi!=1u || info.bytes!=sizeof(info))return 10;
                ui_capabilities=info.capabilities;sdk_stage=SDK_PAGE;
            } else if(sdk_stage==SDK_PAGE)sdk_stage=SDK_WAIT;
            else if(sdk_stage==SDK_FOCUS){if(!focused(&r))return 11;}
            else if(sdk_stage==SDK_FRAME) {
                ++sdk_frame;frame_prepared=0u;input(&r);
                if((ui_capabilities&FX_UI_CAP_FOCUS) && !(sdk_frame%16u))sdk_stage=SDK_FOCUS;
            }
        }
    }
    FxResult result;
    switch(sdk_stage) {
    case SDK_OPEN:result=fx_sdk_open(&sdk,api,"camera.display.scene",1u);break;
    case SDK_QUERY:result=fx_sdk_platform(&sdk,api,FX_BIND_CALL,display_binding,FX_SCENE_QUERY,0u,0,0u);break;
    case SDK_UI_QUERY:result=fx_sdk_ui_query(&sdk,api,0u);break;
    case SDK_FOCUS:result=fx_sdk_ui_query(&sdk,api,1u);break;
    case SDK_PAGE: {
        FxUiControl page={.abi=1u,.bytes=sizeof(page),.id=1u,.kind=FX_UI_PAGE,.flags=FX_UI_VISIBLE|FX_UI_ENABLED};
        if(ui_capabilities&FX_UI_CAP_ROOT)page.flags|=FX_UI_ROOT;
        const char label[]="demo";for(uint32_t i=0;i<sizeof(label);++i)page.label[i]=label[i];
        result=fx_sdk_ui(&sdk,api,&page);break;
    }
    case SDK_FRAME:
        if(!frame_prepared){if(prepare_frame()!=FX_OK)return 7;frame_prepared=1u;}
        result=fx_sdk_scene_submit(&sdk,api,display_binding,1u,scene.width,scene.height,commands,scene.count);break;
    default:result=fx_sdk_events(&sdk,api);break;
    }
    return result==FX_OK?0:8;
}
