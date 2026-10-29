#ifndef FENESTRA_FILE_OBSERVER_H
#define FENESTRA_FILE_OBSERVER_H
#include "fenestra_capacity.h"
/* Trusted, optional normal-task IO observation. It does not replace a native
 * callback, change its result, allocate per-IO metadata, or own a descriptor.
 * Configure before publishing an IO context; the referenced registry can be
 * unpublished until Activate. Claims belong to individual successful opens. */
typedef struct {
    void *context;
    FxCapacityClaim (*opened)(void *,int32_t);
    void (*closed)(FxCapacityClaim *,uint32_t);
    void (*retain)(FxCapacityClaim *);
} FxFileObserver;
static inline FxCapacityClaim fx_file_observe_open(FxFileObserver o,int32_t fd) {
    return o.opened?o.opened(o.context,fd):(FxCapacityClaim){0};
}
static inline void fx_file_observe_close(FxFileObserver o,FxCapacityClaim *c,uint32_t result) {
    if(o.closed)o.closed(c,result);
}
static inline void fx_file_observe_retain(FxFileObserver o,FxCapacityClaim *c) {
    if(o.retain)o.retain(c);
}
FxFileObserver fx_capacity_file_observer(FxCapacityRegistry *);
#endif
