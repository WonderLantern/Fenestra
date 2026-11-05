/* Z8 C3.11 menu construction wrapper (T12 construct port).
 *
 * fx_z8_menu_construct_navigation builds one owned menu from a SETUP-family
 * page using the pinned C311 navigation-table quintuple: terminal 0x3bb,
 * back 0x27f, activation trio {0x286,0x28b,0x2ae}, stay 0x68c, 13-record
 * bound. The stock cursor/notify routes pass through untouched; the
 * activation records are rewritten with the entry's action and the back
 * record routes to the parent page id.
 *
 * fx_z8_menu_construct is the installed-path entry point that reads the
 * navigation table from the pinned image address. */
#include "menu_data.h"
#include "fenestra_native_menu.h"
#include <stddef.h>

_Static_assert(sizeof(FxZ8MenuEvent) == sizeof(FxNativeEvent), "nav event ABI");
_Static_assert(sizeof(FxZ8MenuPage) == sizeof(FxNativePage), "nav page ABI");
_Static_assert(offsetof(FxZ8OwnedMenu, handlers) == offsetof(FxNativeMenu, handlers),
               "owned menu prefix must match the shared menu layout");

FxResult fx_z8_menu_construct_navigation(FxZ8OwnedMenu *out, const FxZ8MenuPage *setup,
                                         uint32_t page_id, uint32_t title_id,
                                         const FxZ8MenuEntry *entries, uint32_t count,
                                         const uintptr_t *callbacks, const FxZ8MenuEvent *navigation) {
    if (!out || !setup || !callbacks || !navigation)
        return FX_ARGUMENT;
    const FxNativeDescriptor *descriptor = (const FxNativeDescriptor *)(uintptr_t)setup->descriptor;
    if (!descriptor || !descriptor->rows)
        return FX_ARGUMENT;
    const FxNativeRow *rows = (const FxNativeRow *)(uintptr_t)descriptor->rows;
    FxNativeMenuProfile profile = {
        (const FxNativePage *)setup, &rows[44], (const FxNativeEvent *)navigation,
        /* C311 quintuple, pinned from the 0x43bf0680 table (see menu_data.h). */
        0x3bbu, 0x27fu, {0x286u, 0x28bu, 0x2aeu, 0u}, 0x68cu,
        setup->identity >> 16, 13u};
    return fx_native_menu_build((FxNativeMenu *)(uintptr_t)out, &profile, page_id, title_id,
                                (const FxNativeEntry *)entries, count,
                                (const uintptr_t *)callbacks);
}

FxResult fx_z8_menu_construct(FxZ8OwnedMenu *out, const FxZ8MenuPage *setup,
                              uint32_t page_id, uint32_t title_id,
                              const FxZ8MenuEntry *entries, uint32_t count,
                              const uintptr_t *callbacks) {
    return fx_z8_menu_construct_navigation(out, setup, page_id, title_id, entries, count,
                                           callbacks,
                                           (const FxZ8MenuEvent *)(uintptr_t)FX_Z8_MENU_NAVIGATION);
}
