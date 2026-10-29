#ifndef FENESTRA_SCENE_SDK_H
#define FENESTRA_SCENE_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_scene.h"
static inline FxResult fx_sdk_scene_capacity(FxSdkClient *c,const FxServiceModule *a,uint64_t binding) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_SCENE_CAPACITY,0u,0,0u);
}
static inline FxSceneCommand fx_scene_clear(uint32_t color){return (FxSceneCommand){FX_SCENE_CLEAR,0u,0u,0u,0u,color};}
static inline FxSceneCommand fx_scene_glyph(uint16_t x,uint16_t y,uint16_t glyph,uint32_t scale,uint32_t color) {
    return (FxSceneCommand){FX_SCENE_GLYPH,x,y,glyph,scale,color};
}
static inline FxSceneCommand fx_scene_rectangle(uint16_t x,uint16_t y,uint16_t width,uint32_t height,uint32_t color) {
    return (FxSceneCommand){FX_SCENE_RECT,x,y,width,height,color};
}
static inline FxSceneCommand fx_scene_image(uint16_t x,uint16_t y,uint16_t width,uint32_t height,uint32_t descriptor_offset) {
    return (FxSceneCommand){FX_SCENE_IMAGE,x,y,width,height,descriptor_offset};
}
static inline FxResult fx_sdk_scene_submit_images(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,uint32_t page,
    uint32_t width,uint32_t height,const FxSceneCommand *commands,uint32_t count,const void *data,uint32_t bytes) {
    if(!commands || !data)return FX_ARGUMENT;
    FxSceneImageSubmission r={{FX_SCENE_ABI,sizeof(r),page,width,height,count,(uintptr_t)commands},(uintptr_t)data,bytes,0u};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_SCENE_SUBMIT,0u,&r,sizeof(r));
}
static inline FxResult fx_sdk_scene_submit(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,uint32_t page,
    uint32_t width,uint32_t height,const FxSceneCommand *commands,uint32_t count) {
    if(!commands)return FX_ARGUMENT;
    FxSceneSubmission r={FX_SCENE_ABI,sizeof(r),page,width,height,count,(uintptr_t)commands};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_SCENE_SUBMIT,0u,&r,sizeof(r));
}
/* Native shooting foreground surface. Query FX_SCENE_CAP_GLOBAL first.
 * A menu/application transition hides it; the module decides when to resubmit. */
static inline FxResult fx_sdk_scene_submit_global(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint32_t width,uint32_t height,const FxSceneCommand *commands,uint32_t count) {
    return fx_sdk_scene_submit(c,a,binding,FX_SCENE_GLOBAL_PAGE,width,height,commands,count);
}
#endif
