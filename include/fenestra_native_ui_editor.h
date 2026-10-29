#ifndef FENESTRA_NATIVE_UI_EDITOR_H
#define FENESTRA_NATIVE_UI_EDITOR_H
#include "fenestra_native_ui.h"
#include "fenestra_native_editor.h"
/* Independently decoded native keyboard presentation/input functions. No
 * original property getter, setter or save/open business callback. */
typedef struct FxNativeUiEditorProfile {
    const FxNativePage *style;
    const FxNativeEvent *events;
    uint32_t count,accept,cancel,back;
    uintptr_t initialize,read,ready,transition,current_page;
} FxNativeUiEditorProfile;
typedef struct FxNativeUiEditor {
    FxNativeEditor native;
    FxNativeUi *ui;
    FxNativePickerView observed;
    FxNativeEditorConfig config;
    uint32_t opened,completed;
    char initial[55],result[55];
} FxNativeUiEditor;
/* Original UI task only. Storage remains retained while native references
 * exist; a closed session can be rebuilt on the next explicit row selection. */
FxNativePage *fx_native_ui_editor_page(FxNativeUi *,const FxNativePage *);
int fx_native_ui_editor_current(const FxNativeUi *);
/* Runtime adapter: exact current owned editor, or NULL. Event conditions in
 * the Nikon ABI have no arguments (unlike page open/close callbacks). */
FxNativeUi *fx_native_ui_editor_input_owner(void);
#endif
