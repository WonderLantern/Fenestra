#ifndef FENESTRA_NATIVE_EDITOR_H
#define FENESTRA_NATIVE_EDITOR_H
#include "fenestra_native_menu.h"

/* Raw original byte editor, resolved through camera.ui.editor.*. It is a
 * singleton on both profiles. Run on original.ui with a live native page;
 * never initialize it over another stock/module editing session. All native
 * pages, callbacks and configuration references must outlive that session.
 * This is NOT a copied FxUiControl or an independent editor per owner. */
typedef struct {
    uint64_t initial,validate; /* Native byte buffer and optional native callback. */
    uint8_t keyboard,layout,padding,reserved;
    int16_t rows,maximum; /* Byte capacity excluding NUL, not Unicode characters. */
} FxNativeEditorConfig;
/* Verified original configuration: keyboard 3, layout 10, padding 0,
 * rows 2/maximum 36 or rows 3/maximum 54. Other configurations unqualified.
 * Initial storage must be readable for maximum bytes. init copies the bytes,
 * but retains the config's native pointer values, including validate.
 * read(out) requires maximum+1 bytes; returns 1 on accepted input, 0 on
 * validation rejection without modifying out. validate(buffer) runs native.
 * insert-byte(byte)/delete-byte() operate on the initialized cursor; insert
 * into a full field discards its final byte. They do not submit a GUI redraw.
 * hit(point,row,column) consumes two int16 coordinates and writes two int16
 * outputs. It tests the field's character position, not whole-screen bounds.
 * No original setting-save callback is part of these five raw bindings. */
_Static_assert(sizeof(FxNativeEditorConfig)==24u,"native editor config ABI");
_Static_assert(offsetof(FxNativeEditorConfig,rows)==20u,"native editor rows ABI");

#define FX_NATIVE_EDITOR_EVENTS 24u
typedef struct {
    FxNativeDescriptor base;
    uintptr_t hit_records;
    uint64_t hit_record_count;
} FxNativeEditorDescriptor;
typedef struct {
    FxNativePage page;
    FxNativeEditorDescriptor descriptor;
    uint8_t header[204];
    FxNativeRow row;
    uintptr_t event_table,callbacks[5];
    FxNativeEvent handlers[FX_NATIVE_EDITOR_EVENTS];
} FxNativeEditor;

/* Explicit event/callback ownership. Only original visual geometry and ROM
 * hit-test data are copied/referenced from style. No source business handler,
 * save callback, navigation destination or setting property is inherited.
 * Include exactly one terminal record, last; all event IDs are unique.
 * The caller supplies the original view-transition callback explicitly too.
 * Build into zeroed, unpublished native storage. Publishing the page, event
 * routing, focus/close and delivering text to the module remain caller work. */
typedef struct {
    const FxNativePage *style,*parent;
    const FxNativeEvent *events;
    uintptr_t callbacks[5];
    uint32_t page,title,row,help,footer,event_count,terminal,reserved;
} FxNativeEditorSpec;
FxResult fx_native_editor_build(FxNativeEditor *,const FxNativeEditorSpec *);
_Static_assert(sizeof(FxNativeEditorDescriptor)==80u,"native editor descriptor ABI");
_Static_assert(sizeof(FxNativeEditorSpec)==96u,"native editor constructor ABI");
#endif
