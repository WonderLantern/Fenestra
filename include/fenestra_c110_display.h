#ifndef FENESTRA_C110_DISPLAY_H
#define FENESTRA_C110_DISPLAY_H
#include "fenestra_native_display.h"
#include "fenestra.h"
#include "fenestra_native_visibility.h"
#include <stdatomic.h>
/* C1.10 native GUI base object. All mutable storage belongs to Fenestra.
 * The original GUI list owns a reference only between attach and detach. */
typedef FxNativeDisplay FxC110Display;
typedef FxNativeView FxC110View;
FxC110View fx_c110_current_view(void);
FxResult fx_c110_display_attach_global(FxC110Display *,const FxDemoScene *);
FxResult fx_c110_display_attach(FxC110Display *,void *,const FxDemoScene *);
FxResult fx_c110_display_present(FxC110Display *,const FxDemoScene *);
void fx_c110_display_detach(FxC110Display *);
FxResult fx_c110_display_refresh_text(void *,uint32_t,uint32_t);
FxResult fx_c110_display_prepare_text(void *,uint32_t,uint32_t);
/* Page-open metadata initialization only; no drawing or callback re-entry. */
FxResult fx_c110_display_prepare_layout(void *,const FxNativeLayoutProfile *);
/* Core callback updates copied UI state under the same native GUI exclusion
 * as cache release and drawing. It must not invoke module code. */
FxResult fx_c110_display_refresh_page(void *,FxResult (*)(void *,void *,uint32_t *),FxResult (*)(void *));
void *fx_c110_display_current_page(uint32_t expected_id);
uint32_t fx_c110_menu_row_state(uint32_t row_id,int *enabled);
#endif
