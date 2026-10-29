#ifndef FENESTRA_NATIVE_SCENE_H
#define FENESTRA_NATIVE_SCENE_H
#include "fenestra_scene.h"
#include "fenestra_native_display.h"
/* Internal retained provider state, not a module wire format. */
typedef struct FxNativeSceneState FxNativeSceneState;
#define FX_NATIVE_SCENE_BUDGET (64u*1024u*1024u)
typedef struct {
    FxProviderContext retained;FxAllocationObserver observer;
    _Atomic(FxNativeSceneState *) head,active;
    uint64_t used,peak,contexts,published;
} FxNativeSceneBudget;
struct FxNativeSceneState {
    FxPlatformOwner *owner;uint64_t token;
    _Atomic uint64_t gate;
    _Atomic uint32_t page;
    uint32_t registered,reference,retired,error;
    FxSceneStore store;FxNativeDisplay display;
    FxAllocationObserver observer;uint32_t retained_mask;
    FxNativeSceneBudget *budget;FxNativeSceneState *next;
};

FxResult fx_native_scene_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,
    uint64_t *,const FxServiceRequest *,FxServiceReply *);
void fx_native_scene_closed(FxPlatformOwner *,uint32_t native_page);
void fx_native_scene_poll(FxPlatformOwner *);
int fx_native_scene_needs_poll(FxPlatformOwner *);
#endif
