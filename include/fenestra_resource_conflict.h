#ifndef FENESTRA_RESOURCE_CONFLICT_H
#define FENESTRA_RESOURCE_CONFLICT_H
#include "fenestra.h"
#include <stdatomic.h>

/* Coordination among cooperating modules, not exclusion of stock firmware.
 * Claim storage belongs to a stable core-owned lease; never to module RAM. */
enum {FX_RESOURCE_SHARED=1u,FX_RESOURCE_EXCLUSIVE=2u};
enum {FX_RESOURCE_MEMORY=1u,FX_RESOURCE_NAMED=2u};
typedef struct FxResourceRegistry FxResourceRegistry;
typedef struct FxResourceClaim FxResourceClaim;
typedef struct {
    uint32_t abi,bytes,requester,generation,holder,holder_generation,requested_mode,held_mode;
    char requested[64],held[64];
} FxResourceConflict;
_Static_assert(sizeof(FxResourceConflict)==160u,"copied resource conflict ABI");
struct FxResourceRegistry {
    _Atomic uint32_t lock;
    uint32_t count;
    FxResourceClaim *head;
};
struct FxResourceClaim {
    FxResourceRegistry *registry;
    FxResourceClaim *next,*previous;
    uint64_t address,bytes;
    uint32_t owner,generation,mode,domain;
    char name[64];
};
/* Nonblocking. BUSY is transient lock contention; CONFLICT names the actual
 * current holder. No callbacks, allocation, firmware IO or implicit eviction. */
FxResult fx_resource_acquire(FxResourceRegistry *,FxResourceClaim *,uint32_t owner,uint32_t generation,
    const char *,uint32_t domain,uint64_t address,uint64_t bytes,uint32_t mode,FxResourceConflict *);
FxResult fx_resource_release(FxResourceClaim *);
/* Atomic release of a contiguous claim array belonging to one registry. */
FxResult fx_resource_release_many(FxResourceClaim *,uint32_t count);
#endif
