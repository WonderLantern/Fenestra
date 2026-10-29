#ifndef FENESTRA_NATIVE_VISIBILITY_H
#define FENESTRA_NATIVE_VISIBILITY_H
#include "fenestra_native_menu.h"
typedef struct {
    uintptr_t current_page,row_lookup,set_active,row_draw,child_lookup;
    uint32_t row_bytes,arrow_child,value_child;
} FxNativeVisibilityProfile;
typedef struct {
    uintptr_t current_page,count_rows,highlight,reset_check,repair_selection,place_rows,resize_canvas,scrollbar;
    uintptr_t prepare_viewport;
} FxNativeLayoutProfile;
/* Original row placement, canvas sizing, selection and scrollbar primitives. No page-open callback,
 * navigation, GUI lock acquisition, drawing or compositor submission. */
FxResult fx_native_row_reflow(const FxNativeLayoutProfile *,FxNativePage *);
/* Native GUI exclusion is held by the caller. Toggle native draw participation
 * only. Highlight/selection belongs to native navigation and is preserved;
 * child icons/check indicators retain their original factory attributes. */
FxResult fx_native_row_visibility(const FxNativeVisibilityProfile *,FxNativePage *,uint32_t row,int visible);
/* Native extended-draw ABI. Owned inactive fixed slots retain old bounds;
 * they must not erase another row after an original layout compacts the list. */
uint32_t fx_native_row_draw_guard(void *row,void *clip,void *progress);
/* Only the reserved native arrow child is toggled; other children stay intact. */
FxResult fx_native_row_arrow(const FxNativeVisibilityProfile *,FxNativePage *,uint32_t row,int submenu);
/* Text values have independent native draw participation. An invisible row
 * must also hide its value; preserve disabled, source and cache ownership. */
FxResult fx_native_row_value_visibility(const FxNativeVisibilityProfile *,FxNativePage *,uint32_t row,int visible);
#endif
