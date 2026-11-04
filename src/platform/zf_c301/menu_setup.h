#ifndef FENESTRA_ZF_C301_MENU_SETUP_H
#define FENESTRA_ZF_C301_MENU_SETUP_H

#include "fenestra_native_menu.h"

/* Private, unpublished copy. It does not install a page or own its inputs. */
#define FX_ZF_SETUP_STOCK_ROWS 47u
#define FX_ZF_SETUP_EXTRA_ROWS 2u
#define FX_ZF_SETUP_TOTAL_ROWS (FX_ZF_SETUP_STOCK_ROWS + FX_ZF_SETUP_EXTRA_ROWS)
#define FX_ZF_STOCK_TEXT_COUNT 2898u
#define FX_ZF_SETUP_HEADER_BYTES 272u
typedef struct {
    FxNativeDescriptor descriptor;
    FxNativeRow rows[FX_ZF_SETUP_TOTAL_ROWS];
    uintptr_t events[FX_ZF_SETUP_TOTAL_ROWS];
    FxNativeEvent entry_events[FX_ZF_SETUP_EXTRA_ROWS][FX_NATIVE_EVENTS];
} FxZfSetupCopy;

/* output must be zero-initialized and remain alive as long as any native UI
 * references it. The caller must independently validate source mappings,
 * GUI exclusion, text-bank publication and the trusted action callback.
 * Retained stock event tables can exceed FX_NATIVE_EVENTS: the caller must
 * verify their full extents, indirect targets and lifetimes, including no
 * overlap with output. The retained stock header must stay mapped, valid and
 * immutable for the same lifetime. This function rejects only obvious direct
 * overlaps.
 * Each retained navigation callback and condition must be validated in the
 * Setup-page context before publication; the template's origin alone is not
 * evidence those handlers are appropriate for this page.
 * The caller must serialize writers to output and publish it only after
 * successful return; the busy check and construction are not atomic. */
FxResult fx_zf_setup_prepare(FxZfSetupCopy *output, const FxNativeDescriptor *stock,
                             const FxNativeEvent *navigation, uintptr_t action);
#endif
