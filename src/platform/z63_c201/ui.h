#ifndef FX_Z63_UI_H
#define FX_Z63_UI_H
#include "fenestra_native_menu.h"
#include "fenestra_native_ui.h"
typedef struct FxZ63Ui FxZ63Ui;
FxZ63Ui *fx_z63_ui_create(const FxNativeMenuProfile *);
FxZ63Ui *fx_z63_menu_ui(void);
FxResult fx_z63_menu_text_ensure(uint32_t);
FxResult fx_z63_ui_register(FxPlatformOwner *);
FxNativePage *fx_z63_ui_page(uint32_t);
uint32_t fx_z63_ui_manage(void *);
FxResult fx_z63_ui_freeze(void);
void fx_z63_ui_shutdown(void);
void fx_z63_ui_ready(void);
int fx_z63_ui_text(uint32_t,const uint16_t **,uint32_t *);
#endif
