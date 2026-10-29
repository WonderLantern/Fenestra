#ifndef FENESTRA_NATIVE_TITLE_H
#define FENESTRA_NATIVE_TITLE_H
#include "fenestra_native_menu.h"

typedef struct {
    uintptr_t current_page,lookup,release_text,mark_dirty,destructor,modal_destructor;
    uint32_t modal_id;
} FxNativeTitleProfile;
/* Caller owns the original GUI exclusion. The copied native header supplies
 * the title's resource and widget IDs; no page rebuild or module pointer. */
FxResult fx_native_title_object(const FxNativeTitleProfile *,FxNativePage *,void **);
FxResult fx_native_title_prepare(const FxNativeTitleProfile *,FxNativePage *);
#endif
