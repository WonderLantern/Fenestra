#ifndef FX_ZF_C301_LIFECYCLE_H
#define FX_ZF_C301_LIFECYCLE_H
#include <stdint.h>
#include <stdatomic.h>

enum {
    FX_ZF_LIFE_STARTED=1u, FX_ZF_LIFE_CARD=2u, FX_ZF_LIFE_STOP=4u,
    FX_ZF_LIFE_START_PENDING=0x100u, FX_ZF_LIFE_CARD_PENDING=0x200u,
    FX_ZF_LIFE_STOP_PENDING=0x400u,
    FX_ZF_LIFE_PENDING=0x700u
};
/* Core-owned, zero before publication, retained until reboot. One atomic word
 * carries current state and coalesced work notices. Not an ordered event log.
 * No work is performed by native SYSCTL callbacks. STOP is terminal until
 * reboot; it does not certify a clean shutdown or quiesced native work. */
typedef struct { _Atomic uint32_t word; } FxZfLifecycle;

void fx_zf_lifecycle_record(FxZfLifecycle *,uint32_t event);
/* A single owner consumes notices. Returned state/notices share the atomic
 * fetch-and linearization point. New notices after it remain pending. */
uint32_t fx_zf_lifecycle_take(FxZfLifecycle *);
/* Snapshot predicate only. The owner must serialize actual work admission,
 * revocation and in-flight lifetime; a concurrent STOP may follow this read. */
int fx_zf_lifecycle_accepts_work(const FxZfLifecycle *);

/* Target entry must supply the retained owner, or NULL before publication.
 * The accessor must be callback-safe, nonallocating and nonblocking. The
 * experimental owner_native.c provides it through the retained descriptor
 * owner; it is not a production-qualified global RAM slot. */
FxZfLifecycle *fx_zf_runtime_lifecycle(void);
void fx_zf_before_bs_shutdown_event(uint32_t,uint32_t);
void fx_zf_after_startup_complete(uint32_t,uint32_t);
void fx_zf_after_card_mount(uint32_t,uint32_t);
void fx_zf_before_card_remove(uint32_t,uint32_t);
#endif
