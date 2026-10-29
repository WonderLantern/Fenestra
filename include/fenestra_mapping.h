#ifndef FENESTRA_MAPPING_H
#define FENESTRA_MAPPING_H
#include "fenestra_services.h"
/* rtos.mappings/v1. Native mappings, on original RTOS tables, NOT module EL0
 * mappings. The caller supplies an unused VA and a valid physical resource.
 * Synchronize original table writers, cache aliases and DMA before mutation.
 * No backing RAM allocation, MMIO initialization or Flash operation is implied. */
enum {FX_MAPPING_QUERY=1u,FX_MAPPING_MAP,FX_MAPPING_TRANSLATE,
      FX_MAPPING_PROTECT,FX_MAPPING_RELEASE,FX_MAPPING_RECLAIM,FX_MAPPING_MAP_ROOT,FX_MAPPING_TRANSLATE_ROOT,
      FX_MAPPING_MAP_LOCKED,FX_MAPPING_TRANSLATE_LOCKED};
typedef struct {uint64_t handle,address,physical,bytes;uint32_t attributes,reserved;} FxMappingRequest;
/* Root operations select original roots[0..2]; legacy requests are unchanged.
 * QUERY's operations mask advertises support. These do not switch TTBR/ASID. */
typedef struct {FxMappingRequest mapping;uint32_t root,reserved;} FxMappingRootRequest;
/* LOCKED operations: root=UINT32_MAX selects the base, 0..2 selects roots[].
 * The caller supplies the actual live 16-byte native CPU lock used by the
 * original writers. The lock address is retained by a successful/partial MAP
 * until RECLAIM; PROTECT/RECLAIM subsequently use it without a new address.
 * All users must obey this protocol. It does not discover a global MMU lock,
 * reserve physical memory, stop DMA or establish cache attribute ownership. */
typedef struct {FxMappingRequest mapping;uint64_t writer_lock;uint32_t root,reserved;} FxMappingLockedRequest;
/* Replies for operations that attempted the native lock include this status.
 * result is the mapping-operation FxResult (STATE if acquire prevented it).
 * value[3] retains the first native error; release is always reported here.
 * Lock contention (-1009) returns BUSY without touching a table or bitmap. */
typedef struct {int32_t acquire,release;uint32_t result,reserved;} FxMappingLockStatus;
typedef struct {uint32_t abi,bytes,operations,capacity,max_bytes,page_bytes,active,retained,private_roots,reserved;} FxMappingInfo;
/* MAP: handle=0, aligned VA/PA/length. Returns handle,VA,length in value[0..2].
 * Even an original partial-map error returns a RETAINED handle: RECLAIM after
 * quiescing all users. value[3] preserves sign-extended native errors.
 * TRANSLATE: handle=0,address=VA; returns PA,original attributes. No allocation.
 * PROTECT: handle and attributes only; callers have already quiesced users and
 * performed required cache maintenance. Explicit TLB invalidation follows.
 * RELEASE: handle only; retains address/table/backing references, no unmap.
 * RECLAIM: handle only; caller asserts all users/aliases/DMA have quiesced.
 * Last binding close and Panic retain all published mappings. No auto-unmap.
 * The shared lock coordinates Fenestra owners, NOT independent stock writers.
 * MAP_ROOT/TRANSLATE_ROOT use FxMappingRootRequest. MAP_ROOT leases pin the
 * selected table; subsequent PROTECT/RELEASE/RECLAIM use the normal handle.
 * A changed root pointer causes INTEGRITY, retaining the lease for recovery.
 * Base mutations return STATE in original private-root mode because its
 * default-selector path recurses. Explicit root operations instead use a
 * temporary context view and commit only its shared allocation bitmap.
 * The caller owns the selected tree: shared lower tables are NOT copied.
 * Original writers, cache aliases, CPU users and DMA require caller exclusion.
 * Named raw rtos.mmu.* functions remain available with their original ABI. */
#endif
