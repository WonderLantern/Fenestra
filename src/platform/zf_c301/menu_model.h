#ifndef FX_ZF_MENU_MODEL_H
#define FX_ZF_MENU_MODEL_H
#include "menu_setup.h"
#include "fenestra_native_font.h"
#include "fenestra_native_modules.h"
#include "fenestra_native_apps.h"
#include "fenestra_native_ui_pager.h"
#define FX_ZF_MENU_TEXT_BASE 2898u
#define FX_ZF_MENU_LABELS 26u
#define FX_ZF_MANAGER_TEXT_BASE (FX_ZF_MENU_TEXT_BASE+FX_ZF_MENU_LABELS)
#define FX_ZF_MODULE_TEXT_BASE (FX_ZF_MANAGER_TEXT_BASE+FX_NATIVE_UI_TEXTS)
#define FX_ZF_MODULE_TEXT_STRIDE (FX_NATIVE_UI_TEXTS+2u)
typedef struct {uintptr_t activate,manage,usb_toggle,auto_on,auto_off,clear_guard;} FxZfMenuActions;
typedef struct {
    _Atomic uint32_t ready,opened,lookups,texts;
    _Atomic uint32_t activation_requested;
    FxNativeTextBank *bank;
    FxNativeMenuProfile profile;
    FxNativeModules *modules_manager;
    FxNativeApps apps;
    FxNativeUiSpace ui_space;
    _Atomic uint32_t manager_accounted;
    FxNativePage setup_page;
    FxZfSetupCopy setup;
    FxNativeMenu core,wonder,auto_menu,guard_menu;
} FxZfMenu;
/* Unpublished construction; all native sources and callbacks remain retained.
 * No globals, allocation, GUI calls or module execution in this constructor. */
FxResult fx_zf_menu_prepare(FxZfMenu *,const FxNativePage *,const FxNativeEvent *,const uintptr_t[5],const FxZfMenuActions *);
int fx_zf_menu_label(int32_t,const uint16_t **,uint32_t *);
int fx_zf_menu_modules_label(const uint16_t **,uint32_t *);
FxResult fx_zf_menu_install(void);
FxNativePage *fx_zf_menu_page(uint32_t,uint32_t);
void fx_zf_menu_text(int32_t,int32_t,const uint16_t **,uint32_t *);
uint64_t fx_zf_menu_navigate(uint32_t);
void fx_zf_menu_refresh_core(void);
/* Native page-open path shared by the root menu and module-owned pages.
 * Module pages need their hidden reserved rows applied before first paint. */
uint32_t fx_zf_menu_opened_native(FxNativePage *);
FxResult fx_zf_menu_ensure_text(FxZfMenu *,uint32_t);
/* Original GUI task, before activation admits module service owners. */
struct FxZfOwnerStorage;
FxResult fx_zf_menu_resources_prepare(FxZfMenu *,struct FxZfOwnerStorage *);
FxNativePage *fx_zf_menu_apps_page(FxZfMenu *,uint32_t);
int fx_zf_menu_module_text(FxZfMenu *,uint32_t,const uint16_t **,uint32_t *);
#endif
