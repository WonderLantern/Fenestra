#ifndef FENESTRA_NATIVE_FONT_H
#define FENESTRA_NATIVE_FONT_H
#include <stdint.h>
#include <stdatomic.h>
#include "fenestra_module_index.h"
#include "fenestra_text_regions.h"

/* Original font requests hold one native mutex until the font worker replies.
 * These adapters extend that existing exclusion; they never run a font worker
 * operation concurrently or change the camera's selected language. */
typedef struct {
    uintptr_t lock, unlock, engine_root, open_font, select_font;
    uintptr_t fallback_data;
    uint32_t fallback_bytes;
    uintptr_t measure, set_layout;
} FxNativeFontProfile;
typedef struct {
    uintptr_t engine, face, saved;
    uint32_t active, error;
} FxNativeFont;

/* GUI-writer owned native resource maps. A published generation is retained:
 * stock widgets may still hold its roots. New IDs are published only in new
 * native widgets after the expanded roots have been installed by the profile. */
#define FX_NATIVE_TEXT_BUDGET (8u * 1024u * 1024u)
#define FX_NATIVE_TEXT_LIMIT 65536u
typedef struct FxNativeTextBank {
    const struct FxNativeTextBank *previous;
    uint32_t stock,capacity,languages,bytes,retained_bytes;
    uint8_t font_default,attribute_default;
    uint8_t *fonts;uintptr_t *attributes;
    _Atomic uint32_t accounted;

} FxNativeTextBank;
FxResult fx_native_text_bank_create(FxIndexMemory,const uint8_t *fonts,const uintptr_t *attributes,
    uint32_t stock,uint32_t languages,uint32_t required,uint8_t font,uint8_t attribute,FxNativeTextBank **);
FxResult fx_native_text_bank_grow(FxIndexMemory,FxNativeTextBank *,uint32_t required,FxNativeTextBank **);

/* A null state keeps the original lock/unlock behavior, including before
 * activation. enter/leave must be paired at the original request boundaries. */
void fx_native_font_enter(FxNativeFont *, const FxNativeFontProfile *,
                          const FxTextRegions *,const void *request);
void fx_native_font_leave(FxNativeFont *, const FxNativeFontProfile *);
/* Original minimum-line-height probe: retry a clipped result with natural
 * height, then restore the original layout through its API. */
uint32_t fx_native_font_line_metrics(FxNativeFont *, const FxNativeFontProfile *,
                                      uintptr_t engine, uintptr_t layout, int32_t dimensions[2]);
#endif
