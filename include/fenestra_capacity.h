#ifndef FENESTRA_CAPACITY_H
#define FENESTRA_CAPACITY_H
#include "fenestra_platform.h"
/* Advisory observation of original shared resources, never admission control.
 * Limits describe the whole native domain, not currently free capacity. */
#define FX_CAPACITY_METADATA_BUDGET (1024u*1024u)
enum {FX_CAPACITY_QUERY=1u,FX_CAPACITY_ENUM,FX_CAPACITY_WATCH,FX_CAPACITY_CANCEL};
enum {FX_CAPACITY_PARTIAL=1u,FX_CAPACITY_APPROXIMATE=2u,FX_CAPACITY_KNOWN_LIMIT=4u,
      FX_CAPACITY_METADATA_FAILED=8u,FX_CAPACITY_INVALID=16u,FX_CAPACITY_VIEW=32u};
typedef struct FxCapacityRegistry FxCapacityRegistry;
typedef struct FxCapacityDomain FxCapacityDomain;
typedef struct FxCapacityWatch FxCapacityWatch;
struct FxCapacityRegistry {
    FxIndexMemory memory;FxAllocationObserver metadata;
    FxCapacityDomain *domains;
    _Atomic uint32_t ready,flags,metadata_used,metadata_peak;
    uint32_t domain_count;
};
struct FxCapacityDomain {
    FxCapacityDomain *next;FxCapacityRegistry *registry;
    char name[64];uint64_t id,limit;uint32_t exhausted,flags;
    _Atomic uint64_t used,peak,retained,acquired,failed,pressure,released,epoch;
    _Atomic uint32_t last_error;
    _Atomic uint32_t active_watches;
    _Atomic(FxCapacityWatch *) watches;
};
/* One trusted descriptor owns one claim. Caller serializes its operations;
 * only aggregate counters and producer/subscriber publication are concurrent.
 * Never publish this address. A failed close can outlive this local record. */
typedef struct {FxCapacityDomain *domain;uint64_t units;uint32_t retained;} FxCapacityClaim;
typedef struct {char name[64];uint64_t observed;} FxCapacityRequest;
typedef struct {
    uint32_t abi,bytes,flags,last_error;uint64_t id,limit;
    char name[64];
    uint64_t used,peak,retained,acquired,failed,pressure,released,epoch;
} FxCapacityInfo;
_Static_assert(sizeof(FxCapacityRequest)==72u,"capacity request ABI");
_Static_assert(sizeof(FxCapacityInfo)==160u,"capacity observation ABI");
/* Configure unpublished, zeroed registry/domain storage before Activate
 * publishes ready. All nodes have controller/reboot lifetime. No fixed count. */
FxResult fx_capacity_init(FxCapacityRegistry *,FxIndexMemory,FxAllocationObserver);
FxResult fx_capacity_register(FxCapacityRegistry *,FxCapacityDomain *,const char *,uint64_t limit,uint32_t exhausted);
/* Snapshot existing claims into an unpublished domain; no invented history or
 * release notification. Observed units may exceed an advisory native limit. */
FxResult fx_capacity_seed(FxCapacityDomain *,uint64_t used,uint64_t retained);
void fx_capacity_publish(FxCapacityRegistry *);
FxCapacityDomain *fx_capacity_find(FxCapacityRegistry *,const char *);
FxCapacityClaim fx_capacity_acquire(FxCapacityDomain *,uint64_t units);
void fx_capacity_failed(FxCapacityDomain *,uint32_t native_error);
void fx_capacity_retain(FxCapacityClaim *);
void fx_capacity_release(FxCapacityClaim *,uint32_t native_result);
void fx_capacity_info(const FxCapacityDomain *,FxCapacityInfo *);
/* Native IO wrappers observe after the original call. Negative open results
 * retain their exact bits; zero-success close errors retain the claim. */
FxCapacityClaim fx_capacity_file_open(FxCapacityDomain *,int32_t native_result);
FxResult fx_capacity_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);
/* Externally proven quiescent registry/owners only, for fixtures or an
 * unpublished controller. Never a hot-unload operation. */
FxResult fx_capacity_destroy(FxCapacityRegistry *);
/* Profile-owned accessor. Returns NULL before local Activate initializes it. */
FxCapacityRegistry *fx_native_capacity_registry(void);
#endif
