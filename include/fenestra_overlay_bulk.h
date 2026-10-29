#ifndef FENESTRA_OVERLAY_BULK_H
#define FENESTRA_OVERLAY_BULK_H
#include "fenestra_overlay.h"
#define FX_OVERLAY_BULK_CHUNK 65536u
enum { FX_OVERLAY_BULK_READING,FX_OVERLAY_BULK_READY,FX_OVERLAY_BULK_FAILED,FX_OVERLAY_BULK_CANCELLED };
typedef struct {
    void *context;
    /* Already-open, owned, synchronous read-only source. Never a module
     * callback/pointer. close failure leaves ownership uncertain. */
    FxResult (*read)(void *,void *,uint32_t,uint32_t *);
    FxResult (*close)(void *);
} FxOverlaySource;
typedef struct {
    FxOverlay *store;FxOverlayActor actor;uint64_t stage;
    FxOverlaySource source;uint32_t phase,close_attempted,closed;
} FxOverlayBulk;
/* A trusted background preparer reads directly into private staging RAM in
 * at most64KiB steps; the768B SDK channel remains the control path. BUSY means
 * progress/pending and requires yielding, not an unsuccessful transaction.
 * EOF, source close and SHA256 must pass before READY. Never publishes a model.
 * Panic retains the snapshot/source; no implicit close/release or retry. */
FxResult fx_overlay_bulk_init(FxOverlayBulk *,FxOverlay *,FxOverlayActor,uint64_t,FxOverlaySource);
FxResult fx_overlay_bulk_step(FxOverlayBulk *);
FxResult fx_overlay_bulk_cancel(FxOverlayBulk *);
#endif
