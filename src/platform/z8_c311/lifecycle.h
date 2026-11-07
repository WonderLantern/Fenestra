#ifndef FX_Z8_C311_LIFECYCLE_H
#define FX_Z8_C311_LIFECYCLE_H
#include <stdint.h>
#include <stdatomic.h>

/* Z8 C3.11 system-event ids: names byte-verified against the pinned event
 * table at 0x43F907F8 (64 entries x 88 bytes) and re-checked on every core
 * build. The Zf reference platform numbers the same lifecycle events
 * 8/20/24/25; do not transpose its ids onto this target. */
enum {
    FX_Z8_LIFE_EVENT_BS_SHUTDOWN = 7u,      /* eSyscCommandId_BSShutdown */
    FX_Z8_LIFE_EVENT_STARTUP_COMPLETE = 19u,/* eSyscCommandId_AppFirstStartComplete */
    FX_Z8_LIFE_EVENT_CARD_REMOVE = 23u,     /* eSyscCommandId_CardRemove */
    FX_Z8_LIFE_EVENT_MOUNT_COMPLETE = 24u,  /* eSyscCommandId_CardMountComplete */
};

enum {
    FX_Z8_LIFE_STARTED=1u, FX_Z8_LIFE_CARD=2u, FX_Z8_LIFE_STOP=4u,
    /* Sticky witness: set by the first mount-complete record and never
     * cleared (not a pending notice; excluded from FX_Z8_LIFE_PENDING).
     * It separates the boot cleanup window from every later card cycle. */
    FX_Z8_LIFE_MOUNT_EVER=0x8u,
    FX_Z8_LIFE_START_PENDING=0x100u, FX_Z8_LIFE_CARD_PENDING=0x200u,
    FX_Z8_LIFE_STOP_PENDING=0x400u,
    FX_Z8_LIFE_PENDING=0x700u
};
/* Core-owned, zero before publication, retained until reboot. One atomic word
 * carries current state and coalesced work notices. Not an ordered event log.
 * No work is performed by native SYSCTL callbacks. STOP is terminal until
 * reboot; it does not certify a clean shutdown or quiesced native work. */
typedef struct { _Atomic uint32_t word; } FxZ8Lifecycle;

void fx_z8_lifecycle_record(FxZ8Lifecycle *,uint32_t event);
/* A single owner consumes notices. Returned state/notices share the atomic
 * fetch-and linearization point. New notices after it remain pending. */
uint32_t fx_z8_lifecycle_take(FxZ8Lifecycle *);
/* Snapshot predicate only. The owner must serialize actual work admission,
 * revocation and in-flight lifetime; a concurrent STOP may follow this read. */
int fx_z8_lifecycle_accepts_work(const FxZ8Lifecycle *);
/* Pure predicate over a lifecycle-word snapshot and a startup-service phase
 * value (the FX_STARTUP_* enum in fenestra_startup.h). Returns 1 exactly
 * when no mount completion has ever been recorded and the service has not
 * yet reached ARMED: the E2c evidence window in which the camera emits its
 * boot-time cleanup CardRemove (23) through the sysctl gate (both card
 * states, always before the first 24). The gate uses it to exempt that
 * event from the stop handshake; FAULT/STOPPED and every post-mount word
 * fail closed. Callback-safe: no dereference, no ordering requirement. */
int fx_z8_lifecycle_boot_cleanup_window(uint32_t word,uint32_t phase);

/* Target entry must supply the retained owner, or NULL before publication.
 * The accessor must be callback-safe, nonallocating and nonblocking. */
FxZ8Lifecycle *fx_z8_runtime_lifecycle(void);
void fx_z8_before_bs_shutdown_event(uint32_t,uint32_t);
void fx_z8_after_startup_complete(uint32_t,uint32_t);
void fx_z8_after_card_mount(uint32_t,uint32_t);
void fx_z8_before_card_remove(uint32_t,uint32_t);
#endif
