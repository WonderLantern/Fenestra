#ifndef FENESTRA_SLOT_POOL_H
#define FENESTRA_SLOT_POOL_H
#include "fenestra_module_index.h"
#include "fenestra_allocation_observer.h"

/* Owner-lock serialized metadata, never an IRQ allocator. Chunks do not move;
 * only their private pointer index grows. Published objects remain stable.
 * Releasing a slot asserts that every external reference has quiesced. */
#define FX_SLOT_POOL_CHUNK 16u
#define FX_SLOT_POOL_LIMIT 65534u
#define FX_NATIVE_RESOURCE_BUDGET (1024u * 1024u)
#define FX_NATIVE_BUFFER_MAX_BYTES (128u * 1024u * 1024u)
#define FX_NATIVE_BUFFER_BUDGET (512u * 1024u * 1024u)
typedef struct {uint32_t limit,used,peak;} FxSlotBudget;
typedef struct {
    FxIndexMemory memory;FxAllocationObserver observer;FxSlotBudget *budget;
    void **chunks;uint32_t chunk_count,index_capacity,stride,item_bytes;
    uint32_t capacity,issued,free_head,used,retained;
} FxSlotPool;
FxResult fx_slot_pool_init(FxSlotPool *,FxIndexMemory,FxSlotBudget *,uint32_t item_bytes);
FxResult fx_slot_pool_take(FxSlotPool *,void **out,uint64_t *token);
void *fx_slot_pool_get(const FxSlotPool *,uint64_t token);
void *fx_slot_pool_at(const FxSlotPool *,uint32_t index);
FxResult fx_slot_pool_retain(FxSlotPool *,uint64_t token);
FxResult fx_slot_pool_release(FxSlotPool *,uint64_t token);
uint32_t fx_slot_pool_limit(const FxSlotPool *);
/* Only for a pool never published, or after externally proven quiescence. */
FxResult fx_slot_pool_destroy(FxSlotPool *);
#endif
