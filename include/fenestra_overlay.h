#ifndef FENESTRA_OVERLAY_H
#define FENESTRA_OVERLAY_H
#include "fenestra_sha256.h"

#define FX_OVERLAY_ABI 1u
#define FX_OVERLAY_TARGETS 8u
#define FX_OVERLAY_VERSIONS 8u
#define FX_OVERLAY_HANDLES 16u
#define FX_OVERLAY_CHUNK 768u
#define FX_OVERLAY_OBJECT_LIMIT (64u*1024u*1024u)
#define FX_OVERLAY_HASH_STEP 65536u
enum { FX_OVERLAY_SAME_SIZE=1u,FX_OVERLAY_MODEL=2u };
enum { FX_OVERLAY_OFF,FX_OVERLAY_ACTIVE,FX_OVERLAY_QUIESCING,FX_OVERLAY_FAULT,FX_OVERLAY_PREPARING };
enum { FX_OVERLAY_FREE,FX_OVERLAY_WRITING,FX_OVERLAY_READY,FX_OVERLAY_LIVE,FX_OVERLAY_RETIRED };

/* Trusted native bindings grant target IDs to publishers. A module cannot
 * turn a supplied path/address into a new target. A binding may represent a
 * native file or an embedded ROM resource; both keep their original bytes.
 * MODEL requires a nonzero graph/layout/runtime compatibility fingerprint,
 * independently checked by the native policy before publication. */
typedef struct {
    uint32_t id,publisher,flags,max_bytes,original_bytes;
    const uint8_t *original;
    uint8_t original_sha256[32],schema_sha256[32];
    char binding[64];
} FxOverlayTargetSpec;
typedef struct { uint32_t owner,generation; } FxOverlayActor;
typedef struct { uint8_t *data;uint32_t capacity;uint64_t token; } FxOverlayAllocation;
typedef struct {
    void *context;
    /* RAM only, owned for this lifetime. No Flash/device/executable mappings.
     * Failure means no allocation acquired; uncertain failures latch Panic. */
    FxResult (*reserve)(void *,uint32_t,FxOverlayAllocation *);
    /* Failure means ownership uncertain: never discard or retry that token. */
    FxResult (*release)(void *,uint64_t);
} FxOverlayMemory;
typedef struct {
    void *context;
    /* Called with the platform configuration lock AND an actual consumer
     * lease held. Validate lease ownership, stopped/not-yet-loaded consumer,
     * format/schema, and cache/resource constraints. No module boolean is an
     * acknowledgment. Absent callback forbids publish/restore. */
    FxResult (*check)(void *,uint32_t,FxOverlayActor,uint64_t,
                      const uint8_t *,uint32_t,const uint8_t [32]);
} FxOverlayPolicy;
typedef struct {
    FxOverlayTargetSpec spec;uint32_t revision,current,verified,verify_offset;
    FxSha256 original_hash;
} FxOverlayTarget;
typedef struct {
    uint64_t id;uint32_t target,owner,state,written,bytes,base_revision,refs;
    FxOverlayAllocation allocation;FxSha256 hash;uint8_t expected_sha256[32];
} FxOverlayVersion;
typedef struct {
    uint64_t id;uint32_t owner,target,version,position;
} FxOverlayHandle;
typedef struct {
    uint32_t generation,phase,target_count;FxResult first_fault;
    uint64_t budget,retained,next_id;
    FxOverlayMemory memory;FxOverlayPolicy policy;
    FxOverlayTarget targets[FX_OVERLAY_TARGETS];
    FxOverlayVersion versions[FX_OVERLAY_VERSIONS];
    FxOverlayHandle handles[FX_OVERLAY_HANDLES];
} FxOverlay;

/* All entry points are serialized by one owner or a verified platform lock.
 * Storage/policy callbacks must not reenter, except to call panic. No function
 * writes original data, opens a file, persists configuration or installs a hook.
 * A file/resource adapter must preserve native stat/read/seek/close/mapping
 * semantics and propagate selected-overlay errors without falling back.
 * Normal stop closes admission; reboot is the recovery policy, not Deactivate.
 * On Panic retain allocations until independently confirmed system stop. */
void fx_overlay_init(FxOverlay *,uint32_t,uint64_t,FxOverlayMemory,FxOverlayPolicy);
FxResult fx_overlay_register(FxOverlay *,const FxOverlayTargetSpec *);
/* Called only after core Activate. BUSY performs at most HASH_STEP bytes of
 * original verification; yield to the native scheduler before the next call.
 * Original file resolution remains unchanged until preparation completes. */
FxResult fx_overlay_activate(FxOverlay *);
FxResult fx_overlay_begin(FxOverlay *,FxOverlayActor,uint32_t,uint32_t,uint32_t,
                          const uint8_t [32],const uint8_t [32],uint64_t *);
FxResult fx_overlay_feed(FxOverlay *,FxOverlayActor,uint64_t,uint32_t,const void *,uint32_t);
FxResult fx_overlay_seal(FxOverlay *,FxOverlayActor,uint64_t);
FxResult fx_overlay_publish(FxOverlay *,FxOverlayActor,uint64_t,uint32_t,uint64_t);
FxResult fx_overlay_restore(FxOverlay *,FxOverlayActor,uint32_t,uint32_t,uint64_t);
FxResult fx_overlay_abort(FxOverlay *,FxOverlayActor,uint64_t);
FxResult fx_overlay_open(FxOverlay *,FxOverlayActor,uint32_t,uint32_t,uint64_t *,uint32_t *);
FxResult fx_overlay_read(FxOverlay *,FxOverlayActor,uint64_t,void *,uint32_t,uint32_t *);
FxResult fx_overlay_seek(FxOverlay *,FxOverlayActor,uint64_t,int64_t,uint32_t,uint32_t *);
/* Trusted adapters may pin a zero-copy view. Keep the handle until all native
 * async/DMA readers acknowledge completion; modules never receive this pointer. */
FxResult fx_overlay_view(FxOverlay *,FxOverlayActor,uint64_t,const uint8_t **,uint32_t *);
FxResult fx_overlay_close(FxOverlay *,FxOverlayActor,uint64_t);
FxResult fx_overlay_stop(FxOverlay *);
/* Normal shutdown only: native consumer stop acknowledgment for one target.
 * Closing file handles alone never proves that a loaded model has stopped. */
FxResult fx_overlay_drain(FxOverlay *,FxOverlayActor,uint32_t,uint64_t);
FxResult fx_overlay_collect(FxOverlay *);
FxResult fx_overlay_panic(FxOverlay *,FxResult);
#endif
