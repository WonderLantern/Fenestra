#include "special_ui_profile.h"
#include "fx_zf_special_ui_profile.h"
#include "fx_zf_flush_profile.h"
/* Original Zf keyboard presentation events, without stock property/save
 * callbacks. Shared Fenestra constructs its own accept/cancel lifecycle. */
static const FxNativeEvent editor_events[]={
    {0x2acu,0x40517404u,0u,0u},{0x2adu,0x40517408u,0u,0u},
    {0x2aeu,0x4051740cu,0u,0u},{0x2afu,0x40517410u,0u,0u},
    {0x2c6u,0x4051741cu,0u,0u},{0x2c7u,0x40517420u,0u,0u},
    {0x2c8u,0x40517420u,0x403bab94u,0u},
    {0x2c9u,0x4051741cu,0x403bab94u,0u},
    {0x2d7u,0x40517458u,0u,0u},{0x2dau,0x4051745cu,0u,0u},
    {0x32bu,0x40517464u,0u,0u},{0x32du,0x40517468u,0u,0u}
};
const FxNativeUiEditorProfile fx_zf_ui_editor_profile={
    (const FxNativePage *)(uintptr_t)FX_ZF_UI_KEYBOARD_PAGE,editor_events,
    12u,0x2d8u,0x2a7u,0x32fu,
    FX_ZF_UI_KEYBOARD_INITIALIZE,FX_ZF_UI_KEYBOARD_READ,
    FX_ZF_UI_KEYBOARD_READY,FX_ZF_UI_KEYBOARD_TRANSITION,FX_ZF_UI_CURRENT_PAGE
};
const FxNativeDialogStyle fx_zf_ui_dialog={
    (const FxNativeDescriptor *)(uintptr_t)FX_ZF_UI_DIALOG_DESCRIPTOR,
    (const FxNativeRow *)(uintptr_t)FX_ZF_UI_DIALOG_ROWS,340u,2u
};
/* Zf and Z9 modal descriptor/header coordinate layout is the same logical
 * 720x480 UI. Measurements still come from Zf's original text widgets. */
const FxNativeUiReaderProfile fx_zf_ui_reader_profile={
    {FX_ZF_UI_CURRENT_PAGE,FX_ZF_ROW_LOOKUP,FX_ZF_RELEASE_TEXT,
     FX_ZF_TITLE_MARK_DIRTY,FX_ZF_TITLE_DESTRUCTOR,FX_ZF_TITLE_MODAL_DESTRUCTOR,
     FX_ZF_MODAL_ID},FX_ZF_MEASURE,FX_ZF_BOUNDS,720u,480u
};
