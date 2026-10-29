#ifndef FENESTRA_NATIVE_TEXT_H
#define FENESTRA_NATIVE_TEXT_H
#include "fenestra_native_ui.h"
typedef struct {
    uintptr_t current_page,row_lookup,value_lookup,set_disabled,set_text,release_text,row_draw;
    uint32_t value_child,row_bytes,value_bytes;
    uintptr_t set_alignment;
    uintptr_t measure,bounds;
} FxNativeTextProfile;
/* Called solely by original native GUI page-open callback, on its owning
 * task with its normal GUI exclusion. No painting or module pointer input. */
FxResult fx_native_text_prepare(const FxNativeTextProfile *,FxNativePage *,uint32_t row,
                                uint32_t label,uint32_t value,int enabled,FxNativeUi *ui);
#endif
