#include "fenestra_c110_scene.h"
#include "fenestra_native_scene.h"
#include "fenestra_c110_services.h"
#include <stddef.h>
const uint32_t fx_c110_scene_metadata[]={0x46585343u,2u,
    offsetof(FxC110Services,platform),offsetof(FxPlatformOwner,provider_contexts),
    offsetof(FxProviderContext,next),offsetof(FxProviderContext,name),offsetof(FxProviderContext,context),
    offsetof(FxNativeSceneBudget,head),offsetof(FxNativeSceneState,next),
    offsetof(FxNativeSceneState,gate),offsetof(FxNativeSceneState,page),offsetof(FxNativeSceneState,retired),offsetof(FxNativeSceneState,error),
    offsetof(FxNativeSceneState,display)+offsetof(FxNativeDisplay,attached),
    offsetof(FxNativeSceneState,display)+offsetof(FxNativeDisplay,frames),
    offsetof(FxNativeSceneState,display)+offsetof(FxNativeDisplay,readers),
    offsetof(FxNativeSceneState,store)+offsetof(FxSceneStore,frames),offsetof(FxNativeSceneBudget,active)};

FxResult fx_c110_scene_resource(void *c,uint32_t op,const FxNamedBinding *b,FxPlatformOwner *o,uint64_t t,
    uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a){return fx_native_scene_resource(c,op,b,o,t,cookie,q,a);}
void fx_c110_scene_closed(FxPlatformOwner *o,uint32_t page){fx_native_scene_closed(o,page);}
void fx_c110_scene_poll(FxPlatformOwner *o){fx_native_scene_poll(o);}
