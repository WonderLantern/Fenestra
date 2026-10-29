#ifndef FENESTRA_ALLOCATION_OBSERVER_H
#define FENESTRA_ALLOCATION_OBSERVER_H
#include <stdint.h>
#include <stdatomic.h>
/* Trusted cold allocation lifecycle observer. It cannot reject an allocation,
 * change its pointer, or own storage. Set before any allocation/publication. */
enum {FX_ALLOC_REQUESTED=1u,FX_ALLOC_PUBLISHED=2u,FX_ALLOC_FREE_PENDING=3u,FX_ALLOC_FREE_PUBLISHED=4u,FX_ALLOC_UNTRACKED=5u};
typedef struct {void *context;void (*event)(void *,uint32_t bytes,uint32_t operation);} FxAllocationObserver;
/* Published, immutable-lifetime objects which may predate activation. The
 * backend can defer accounting; the atomic tag prevents seeding twice.
 * Deferred roots and the activation seed loads use sequential consistency
 * with backend readiness, so neither side can miss the other's publication. */
typedef struct {void *context;void (*publish)(void *,_Atomic uint32_t *,uint32_t bytes);} FxRetainedAccount;
static inline void fx_retained_account(FxRetainedAccount a,_Atomic uint32_t *tag,uint32_t bytes) {
    if(a.publish)a.publish(a.context,tag,bytes);
}
static inline void fx_allocation_observe(FxAllocationObserver observer,uint32_t bytes,uint32_t operation) {
    if(observer.event)observer.event(observer.context,bytes,operation);
}
#endif
