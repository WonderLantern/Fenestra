#ifndef FENESTRA_ACCOUNTING_H
#define FENESTRA_ACCOUNTING_H
#include "fenestra_module_index.h"
#include "fenestra_allocation_observer.h"
#include <stdatomic.h>
#define FX_ACCOUNT_METADATA_BUDGET (1024u * 1024u)
/* MANAGED counts a directly requested allocation once. VIEW describes bytes
 * already counted elsewhere. EXTERNAL counts objects with unknown native cost.
 * SHARED is an attribution qualifier, never a second owner of the same bytes. */
enum {FX_ACCOUNT_MANAGED=1u,FX_ACCOUNT_VIEW=2u,FX_ACCOUNT_EXTERNAL=4u,FX_ACCOUNT_SHARED=8u,FX_ACCOUNT_STATIC=16u};
enum {FX_ACCOUNT_APPROXIMATE=1u,FX_ACCOUNT_INVALID=2u,FX_ACCOUNT_METADATA_FAILED=4u,FX_ACCOUNT_PARTIAL=8u,FX_ACCOUNT_SEEDED=16u};
enum {FX_ACCOUNT_QUERY=1u,FX_ACCOUNT_ENUM=2u,FX_ACCOUNT_SHARED_QUERY=3u,FX_ACCOUNT_SHARED_ENUM=4u};
typedef struct FxAccountState FxAccountState;
typedef struct FxAccountRow FxAccountRow;
struct FxAccountRow {
    FxAccountRow *next;FxAccountState *state;
    char name[64];uint64_t id,budget;uint32_t flags,reserved;
    _Atomic uint64_t used,peak,retained,units,units_peak,retained_units;
};
struct FxAccountState {
    FxIndexMemory memory;
    FxAccountState *shared; /* Trusted domain ledger; never module supplied. */
    _Atomic(FxAccountRow *) rows;
    _Atomic uint64_t next_id,used,peak,retained;
    _Atomic uint32_t metadata_reserved,metadata_used,row_count,flags;
    uint32_t owner,generation,metadata_limit;
};
/* Snapshots are non-transactional observations of atomic counters. No raw
 * allocator/physical heap size or cross-row same-instant claim is implied. */
typedef struct {uint32_t abi,bytes,owner,generation,flags,rows,metadata_used,metadata_reserved;
    uint64_t used,peak,retained,metadata_budget;} FxAccountInfo;
typedef struct {uint32_t abi,bytes,flags,reserved;uint64_t id,budget;char name[64];
    uint64_t used,peak,retained,units,units_peak,retained_units;} FxAccountRowInfo;
_Static_assert(sizeof(FxAccountInfo)==64u,"account summary ABI");
_Static_assert(sizeof(FxAccountRowInfo)==144u,"account row ABI");
/* Zero-initialize State before init. Published rows and names have owner/reboot lifetime. Allocator must
 * be a raw paired backend, not a recursively instrumented accounting wrapper. */
FxResult fx_account_init(FxAccountState *,uint32_t owner,uint32_t generation,FxIndexMemory);
FxResult fx_account_get(FxAccountState *,const char *name,uint32_t flags,uint64_t budget,FxAccountRow **);
/* Caller-owned persistent storage, initialized once before its publication.
 * Its storage is included in the caller's enclosing allocation, not charged
 * again as dynamic accounting metadata. */
FxResult fx_account_register_static(FxAccountState *,FxAccountRow *,const char *,uint32_t flags,uint64_t budget);
FxResult fx_account_acquire(FxAccountRow *,uint64_t bytes,uint64_t units,uint32_t retained);
FxResult fx_account_release(FxAccountRow *,uint64_t bytes,uint64_t units,uint32_t retained);
FxResult fx_account_retain(FxAccountRow *,uint64_t bytes,uint64_t units);
FxResult fx_account_unretain(FxAccountRow *,uint64_t bytes,uint64_t units);
FxAllocationObserver fx_account_observer(FxAccountRow *);
void fx_account_info(const FxAccountState *,FxAccountInfo *);
/* Stable monotonic row identity, holes allowed. ENUM is not a frozen snapshot. */
FxResult fx_account_next(const FxAccountState *,uint64_t after,FxAccountRowInfo *);
/* Only unpublished or externally proven quiescent state, with every counted
 * object/view released. This is not module deactivation or hot unload. */
FxResult fx_account_destroy(FxAccountState *);
#endif
