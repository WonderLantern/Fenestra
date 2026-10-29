#ifndef FENESTRA_MODULE_INDEX_H
#define FENESTRA_MODULE_INDEX_H
#include "fenestra.h"
#include "fenestra_allocation_observer.h"
/* Transient RAM index, never an on-card catalogue. Records begin with an
 * ASCII module identity[16]. Addresses may move on growth; callers serialize
 * access and must not publish pointers into this store to native consumers. */
#define FX_MODULE_INDEX_BUDGET (1024u * 1024u)
typedef struct {
    void *context;
    void *(*allocate)(void *,uint32_t);
    void (*release)(void *,void *);
} FxIndexMemory;
typedef struct {
    FxIndexMemory memory;
    void *records;uint32_t *buckets;
    uint32_t count,capacity,stride,bytes,budget;
    FxAllocationObserver observer;uint32_t retained;
} FxModuleIndex;
FxResult fx_module_index_init(FxModuleIndex *,FxIndexMemory,uint32_t stride,uint32_t budget);
/* Caller serializes attachment and all mutations. Seeds an existing private
 * allocation once; observers never alter pointers, budgets or admission. */
FxResult fx_module_index_account(FxModuleIndex *,FxAllocationObserver);
void fx_module_index_retain(FxModuleIndex *);
FxResult fx_module_index_reserve(FxModuleIndex *,uint32_t count);
FxResult fx_module_index_find(const FxModuleIndex *,const char *id,uint32_t *slot);
FxResult fx_module_index_add(FxModuleIndex *,const char *id,uint32_t *slot);
void fx_module_index_rehash(FxModuleIndex *);
/* Best effort compaction; OOM leaves the existing valid store intact. */
FxResult fx_module_index_shrink(FxModuleIndex *);
void fx_module_index_destroy(FxModuleIndex *);
#endif
