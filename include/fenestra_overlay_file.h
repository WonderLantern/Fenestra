#ifndef FENESTRA_OVERLAY_FILE_H
#define FENESTRA_OVERLAY_FILE_H

#include "fenestra_overlay_bulk.h"
#include "fenestra_text.h"

/* The adapter supplies canonical names for approved read-only resources.
 * FX_EMPTY tells the adapter to use its original resolver. Handle all other
 * outcomes here. Use the overlay handle for read, seek, view and close. */
FxResult fx_overlay_file_open(FxOverlay *,FxOverlayActor,const char *binding,
                              uint64_t *handle,uint32_t *bytes);

typedef struct {
    FxTextFiles files;
    const char *directory;
    uint32_t missing_error;
} FxOverlayCardSource;

typedef struct {
    uint32_t target,revision,bytes;
    uint8_t original_sha256[32],replacement_sha256[32];
} FxOverlayCardImage;

typedef struct {
    FxOverlayBulk bulk;
    FxTextFiles files;
    int32_t fd;
    uint32_t open,close_attempted;
} FxOverlayCardLoad;

/* Zero-initialize and keep the job at a stable address. Use a trusted card
 * directory ending in a path separator and a single asset basename. The
 * card source context must outlive the file, including uncertain closes.
 * Call fx_overlay_bulk_step on job.bulk until it stops returning FX_BUSY.
 * Then publish the stage with the consumer lease or cancel it. */
FxResult fx_overlay_card_begin(FxOverlayCardLoad *,FxOverlay *,FxOverlayActor,
    const FxOverlayCardSource *,const char *basename,const FxOverlayCardImage *);

#endif
