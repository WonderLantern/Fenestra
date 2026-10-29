#ifndef FENESTRA_C110_SCENE_H
#define FENESTRA_C110_SCENE_H
#include "fenestra_scene.h"
FxResult fx_c110_scene_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,
    uint64_t *,const FxServiceRequest *,FxServiceReply *);
/* GUI close only revokes an epoch: no locks, allocation, renderer or detach. */
void fx_c110_scene_closed(FxPlatformOwner *,uint32_t native_page);
/* Original worker context, after EL0 has returned, including FINISHED owners. */
void fx_c110_scene_poll(FxPlatformOwner *);
#endif
