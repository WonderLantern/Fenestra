#ifndef FENESTRA_TRANSFERS_H
#define FENESTRA_TRANSFERS_H
#include "fenestra_module_index.h"
#include "fenestra_allocation_observer.h"
#include <stdatomic.h>
#define FX_TRANSFER_INLINE 4u
#define FX_TRANSFER_BLOCK 32u
#define FX_TRANSFER_METADATA_BUDGET (1024u*1024u)
typedef struct {
    _Atomic uint64_t tag;
    _Atomic uintptr_t address;
    _Atomic uint32_t bytes;
} FxTransferSlot;
typedef struct FxTransferBlock FxTransferBlock;
struct FxTransferBlock {FxTransferBlock *next;FxTransferSlot slots[FX_TRANSFER_BLOCK];};
typedef struct {
    FxIndexMemory memory;FxAllocationObserver requests,metadata;
    _Atomic(FxTransferBlock *) blocks;
    _Atomic uint32_t metadata_reserved;
    _Atomic uint64_t untracked;
    uint32_t configured,metadata_limit;
    FxTransferSlot inline_slots[FX_TRANSFER_INLINE];
} FxTransfers;
typedef struct {FxTransferSlot *slot;uint64_t tag;uintptr_t address;uint32_t bytes;} FxTransferClaim;
/* Fresh caller-owned state only, before publishing its native callback. The
 * inline slots belong to the enclosing allocation. Dynamic blocks stay stable
 * until reboot; freed slots reuse a generation, not one node per past packet. */
FxResult fx_transfers_init(FxTransfers *,FxIndexMemory,FxAllocationObserver,FxAllocationObserver,uint32_t metadata_budget);
/* Observation cannot deny the operation. Metadata failure conservatively
 * retains this request and records UNTRACKED; a known immediate free can
 * still cancel that caller's untracked claim. */
FxTransferClaim fx_transfers_begin(FxTransfers *,uintptr_t address,uint32_t bytes);
/* Claim before the original receiver performs the native free; complete only
 * after it returns. The receiver never allocates, frees metadata or waits. */
FxTransferClaim fx_transfers_claim(FxTransfers *,uintptr_t address);
void fx_transfers_complete(FxTransfers *,FxTransferClaim *);
/* After a directly owned request's native free has returned. */
void fx_transfers_cancel(FxTransfers *,FxTransferClaim *);
#endif
