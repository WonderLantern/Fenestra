#ifndef FENESTRA_NATIVE_UI_CALLBACKS_H
#define FENESTRA_NATIVE_UI_CALLBACKS_H
#include "fenestra_native_ui.h"
/* Worker-side publication only; the original input task commits copied state
 * under its GUI exclusion. No EL0 callback runs on the GUI task. */
void fx_native_ui_refresh_owner(FxPlatformOwner *);
/* Core-registered retained pages; no compiled owner-index lookup table. */
extern const uintptr_t fx_native_ui_actions[16],fx_native_ui_options[8];
uint32_t fx_native_ui_condition_gate(void);
FxNativeUi *fx_native_ui_find_page(uint32_t page);
uint32_t fx_native_ui_page_open(void *page);
uint32_t fx_native_ui_page_close(void *page);
uint32_t fx_native_ui_noop(void *page);
/* Raw native input bridge for custom module forms on their current page.
 * Call on original.ui after presentation. Copies text/revision into that
 * page owner's queue; does not write the current logical control value. */
FxResult fx_native_ui_text_input(uint32_t control,const char *,uint32_t bytes);
#endif
