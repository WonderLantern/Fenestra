#ifndef FENESTRA_OVERLAY_RANGES_H
#define FENESTRA_OVERLAY_RANGES_H
#include "fenestra_sha256.h"

#define FX_RANGE_HEADER_BYTES 16u
#define FX_RANGE_VERSION_BYTES 120u
#define FX_RANGE_SEGMENT_BYTES 16u
#define FX_RANGE_VERIFY_STEP 65536u

typedef struct {
    const uint8_t *data;
    uint32_t bytes;
    uint64_t offset;
    uint8_t application_sha256[32];
} FxOverlayRangeOriginal;

/* Borrowed immutable recipe and original region. Retain their owners until
 * every consumer has finished. A view stores no patched original bytes.
 * initialize -> bounded verification -> read. Reads preserve total length. */
typedef struct {
    const uint8_t *recipe,*original,*version;
    uint32_t recipe_bytes,bytes,verified,segments,segment_count;
    uint32_t verify_offset;
    FxSha256 original_hash,result_hash;
} FxOverlayRangeView;

FxResult fx_overlay_ranges_init(FxOverlayRangeView *,const void *recipe,uint32_t bytes,
                                 const FxOverlayRangeOriginal *);
FxResult fx_overlay_ranges_verify(FxOverlayRangeView *);
FxResult fx_overlay_ranges_read(const FxOverlayRangeView *,uint32_t offset,
                                void *,uint32_t bytes,uint32_t *actual);
#endif
