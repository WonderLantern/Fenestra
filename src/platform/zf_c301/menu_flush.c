#if !defined(FX_ZF_EXPERIMENTAL_UI_FLUSH_OWNER) || FX_ZF_EXPERIMENTAL_UI_FLUSH_OWNER != 1
#error "Zf flush owner requires the checked experimental GUI profile"
#endif
#include "fenestra_native_ui.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_title.h"
#include "fenestra_native_text.h"
#include "fenestra_native_visibility.h"
#include "fenestra_native_menu.h"
#include "fenestra_native_apps.h"
#include "fenestra_native_display.h"
#include "fx_zf_dispatch_profile.h"
#include "fx_zf_flush_profile.h"
#include "owner_publish.h"
#include "menu_model.h"
#include "ptp.h"

/* Zf C3.01 native front end, experimental. Every function pointer below is an
 * own-Zf window re-derived from the fixed application image; the six ids and
 * offsets come from site-by-site correspondence with the Z9 reference, not from
 * an arithmetic offset (Z9 0x4bec/0x4bf1 -> own 0x4917/0x491d, a difference of
 * 6 rather than 5). All persistent state is immutable; page and module
 * ownership lives in ordinary Nikon heap RAM.
 *
 * The compositor submit uses Zf's own callback 0x403f9d34 and operands (3u,7u),
 * as passed by the native callers at 0x403a7458 and 0x403b9fa4. The callee's
 * internal w1/w2 moves differ from Z9; the caller ABI does not. */

extern const uint8_t __fx_core_code_begin[], __fx_core_code_end[];

#define FN(address, type) ((type)(uintptr_t)(address))

static const FxNativeLayoutProfile ui_layout = {
    FX_ZF_DISPATCH_CURRENT_PAGE,
    FX_ZF_FLUSH_COUNT_ROWS,
    FX_ZF_FLUSH_HIGHLIGHT,
    FX_ZF_FLUSH_RESET_CHECK,
    FX_ZF_FLUSH_REPAIR_SELECTION,
    FX_ZF_FLUSH_PLACE_ROWS,
    FX_ZF_FLUSH_RESIZE_CANVAS,
    FX_ZF_FLUSH_SCROLLBAR,
    FX_ZF_FLUSH_PREPARE_VIEWPORT};

static const FxNativeVisibilityProfile ui_visibility = {
    FX_ZF_DISPATCH_CURRENT_PAGE,
    FX_ZF_ROW_LOOKUP,
    FX_ZF_SET_ACTIVE,
    FX_ZF_ROW_DRAW,
    FX_ZF_CHILD_LOOKUP,
    FX_ZF_ROW_BYTES,
    FX_ZF_ARROW_CHILD,
    FX_ZF_VALUE_CHILD};

static const FxNativeTextProfile ui_text_profile = {
    FX_ZF_DISPATCH_CURRENT_PAGE,
    FX_ZF_ROW_LOOKUP,
    FX_ZF_CHILD_LOOKUP,
    FX_ZF_SET_DISABLED,
    FX_ZF_SET_TEXT,
    FX_ZF_RELEASE_TEXT,
    FX_ZF_ROW_DRAW,
    FX_ZF_VALUE_CHILD,
    FX_ZF_ROW_BYTES,
    FX_ZF_VALUE_BYTES,
    FX_ZF_SET_ALIGNMENT,
    FX_ZF_MEASURE,
    FX_ZF_BOUNDS};

static const FxNativeTitleProfile ui_title = {
    FX_ZF_DISPATCH_CURRENT_PAGE,
    FX_ZF_ROW_LOOKUP,
    FX_ZF_RELEASE_TEXT,
    FX_ZF_TITLE_MARK_DIRTY,
    FX_ZF_TITLE_DESTRUCTOR,
    FX_ZF_TITLE_MODAL_DESTRUCTOR,
    FX_ZF_MODAL_ID};

/* Apply the initial module-page state before Nikon's first paint. The normal
 * owner flush already does this when a revision changes, but the first open
 * has no prior revision and therefore must hide reserved rows explicitly. */
static uint32_t open_module_page(FxNativePage *page) {
    if(!page || !page->descriptor)return 0u;
    uint32_t id=page->identity>>16;
    FxNativeUi *ui=fx_native_ui_find_page(id);
    if(!ui || !fx_native_ui_page_matches(ui,page))return 1u;
    /* Preserve the existing open/refresh lifecycle; the extra work below is
     * the missing first-paint visibility and layout pass. */
    (void)fx_native_ui_page_open(page);
    const FxNativeDescriptor *d=(const void *)page->descriptor;
    const FxNativeRow *rows=(const void *)d->rows;
    for(uint32_t i=0;i<d->count;++i) {
        uint32_t row=rows[i].word[6];
        int visible=fx_native_ui_row_visible(ui,id,row);
        (void)fx_native_row_visibility(&ui_visibility,page,row,visible);
        (void)fx_native_row_arrow(&ui_visibility,page,row,
            visible && fx_native_ui_row_submenu(ui,row));
        int enabled=1;uint32_t value=fx_native_ui_value(ui,row,&enabled);
        (void)fx_native_row_value_visibility(&ui_visibility,page,row,visible && value);
        /* Resolve text/value children for hidden slots during construction;
         * otherwise the native renderer can expose a reserved row on its
         * first pass before the next owner flush repairs it. */
        (void)fx_native_text_prepare(&ui_text_profile,page,row,rows[i].word[0],value,enabled,ui);
    }
    (void)fx_native_row_reflow(&ui_layout,page);
    return 1u;
}

/* Same stock-row value/enabled policy as the Z9 front end, using Zf's
 * checked text/bounds/visibility functions and the retained owner service. */
uint32_t fx_zf_menu_opened_native(FxNativePage *page) {
    FxZfLifecycle *life=fx_zf_runtime_lifecycle();
    if(!life || !page || !page->descriptor)return 0u;
    if(fx_native_ui_find_page(page->identity>>16))return open_module_page(page);
    FxZfOwnerStorage *o=(void *)((uint8_t *)life-offsetof(FxZfOwnerStorage,lifecycle));
    const FxNativeDescriptor *d=(const void *)page->descriptor;
    const FxNativeRow *rows=(const void *)d->rows;
    uint32_t phase=atomic_load(&o->startup.phase);
    for(uint32_t i=0;i<d->count;++i) {
        uint32_t row=rows[i].word[6],value=0u;int enabled=1;
        if(row==0x7ff1u)enabled=phase==FX_STARTUP_READY || phase==FX_STARTUP_IO_ERROR;
        else if(row==0x7ff2u)value=2908u;
        else if(row==0x7ff5u) {
            value=(atomic_load(&o->startup.desired)&FX_STARTUP_AUTO)?2912u:2913u;
            enabled=phase==FX_STARTUP_READY || phase==FX_STARTUP_ABNORMAL || phase==FX_STARTUP_RUNNING;
        } else if(row==0x7ff6u) {
            value=phase==FX_STARTUP_ABNORMAL?2917u:2916u;
            enabled=phase==FX_STARTUP_ABNORMAL && atomic_load(&o->startup.flag_known);
        } else if(row==0x7ff7u) {
            value=fx_zf_ptp_user_enabled()?2912u:2913u;
            enabled=fx_zf_ptp_policy_permits();
        }
        (void)fx_native_text_prepare(&ui_text_profile,page,row,rows[i].word[0],value,enabled,0);
    }
    return 1u;
}
void fx_zf_menu_refresh_core(void) {
    FxZfLifecycle *life=fx_zf_runtime_lifecycle();if(!life)return;
    FxZfOwnerStorage *o=(void *)((uint8_t *)life - offsetof(FxZfOwnerStorage,lifecycle));
    FxZfMenu *menu=(void *)atomic_load_explicit(&o->menu,memory_order_acquire);
    if(!menu || !atomic_load_explicit(&menu->ready,memory_order_acquire))return;
    void *exclusion=(void *)(uintptr_t)FX_ZF_GUI_OBJECT;
    FN(FX_ZF_GUI_ENTER,void (*)(void *))(exclusion);
    int repaint=FN(FX_ZF_DISPATCH_CURRENT_PAGE,void *(*)(void))()==&menu->core.page;
    if(repaint) {
        (void)fx_zf_menu_opened_native(&menu->core.page);
        const FxNativeDescriptor *d=(const void *)menu->core.page.descriptor;
        const FxNativeRow *rows=(const void *)d->rows;
        for(uint32_t i=0;i<d->count;++i) {
            void *row=FN(FX_ZF_ROW_LOOKUP,void *(*)(uint32_t,void *))(rows[i].word[6],(void *)menu->core.page.content);
            if(!row)continue;
            FN(FX_ZF_TITLE_MARK_DIRTY,void (*)(void *,uint32_t))(row,1u);
            if(rows[i].word[6]==0x7ff2u || rows[i].word[6]==0x7ff5u || rows[i].word[6]==0x7ff6u || rows[i].word[6]==0x7ff7u) {
                void *value=FN(FX_ZF_CHILD_LOOKUP,void *(*)(void *,uint32_t))(row,FX_ZF_VALUE_CHILD);
                if(value)FN(FX_ZF_TITLE_MARK_DIRTY,void (*)(void *,uint32_t))(value,1u);
            }
        }
        void *canvases[2]={(void *)menu->core.page.surface,(void *)menu->core.page.content};
        for(uint32_t i=0;i<2u;++i)if(canvases[i]) {
            FN(FX_ZF_DRAW_DIRTY,void (*)(void *,uint32_t))(canvases[i],1u);
            (void)FN(FX_ZF_DRAW_DISPATCH,uint32_t (*)(void *,void *,void *))(canvases[i],0,0);
        }
        FN(FX_ZF_COMMIT_SUBMIT,void (*)(void *,uint32_t,uint32_t))((void *)(uintptr_t)FX_ZF_COMMIT_OBJECT,3u,7u);
    }
    FN(FX_ZF_GUI_LEAVE,void (*)(void *))(exclusion);
}

FxResult fx_native_ui_flush_owner(FxPlatformOwner *owner) {
    void *exclusion = (void *)(uintptr_t)FX_ZF_GUI_OBJECT;
    FN(FX_ZF_GUI_ENTER, void (*)(void *))(exclusion);
    FxNativePage *page = FN(FX_ZF_DISPATCH_CURRENT_PAGE, FxNativePage *(*)(void))();
    FxNativeUi *ui = page ? fx_native_ui_find_page(page->identity >> 16) : 0;
    if (ui && ui->owner == owner) {
        uint32_t destination = 0u;
        FxResult redirect = fx_native_ui_retirement_target(ui, page->identity >> 16, &destination);
        if (redirect == FX_EMPTY) redirect = fx_native_ui_prepare_navigation(ui, page, &destination);
        if (redirect != FX_EMPTY) {
            FN(FX_ZF_GUI_LEAVE, void (*)(void *))(exclusion);
            if (redirect != FX_OK) return redirect;
            (void)FN(FX_ZF_GUI_NAVIGATE, uint64_t (*)(uint32_t))(destination);
            fx_native_ui_refresh_owner(owner);
            return FX_OK;
        }
    }
    uint32_t changed = 0u;
    FxResult refreshed = ui && ui->owner == owner
        ? fx_native_ui_trace(ui, 1u, 0u, fx_native_ui_recover_values(ui, page->identity >> 16, &changed))
        : FX_EMPTY;
    int repaint = refreshed == FX_OK;
    uint32_t painted = 0u;
    if (repaint) {
        const FxNativeDescriptor *d = (const FxNativeDescriptor *)page->descriptor;
        const FxNativeRow *rows = (const FxNativeRow *)d->rows;
        if (changed & FX_NATIVE_UI_TITLE) {
            if (fx_native_ui_trace(ui, 2u, 0u, fx_native_title_prepare(&ui_title, page)) != FX_OK) repaint = 0;
            else painted = 1u;
        }
        if (changed & FX_NATIVE_UI_STRUCTURE) {
            for (uint32_t i = 0; i < d->count; ++i) {
                uint32_t row = rows[i].word[6];
                int enabled = 1;
                int visible = fx_native_ui_row_visible(ui, page->identity >> 16, row);
                uint32_t value = fx_native_ui_value(ui, row, &enabled);
                if (fx_native_ui_trace(ui, 3u, 0u, fx_native_row_visibility(&ui_visibility, page,
                        rows[i].word[6], visible)) != FX_OK ||
                    fx_native_ui_trace(ui, 4u, 0u, fx_native_row_arrow(&ui_visibility, page,
                        rows[i].word[6], visible && fx_native_ui_row_submenu(ui, rows[i].word[6]))) != FX_OK ||
                    fx_native_ui_trace(ui, 5u, 0u, fx_native_row_value_visibility(&ui_visibility, page,
                        row, visible && value)) != FX_OK) repaint = 0;
            }
            if (repaint && fx_native_ui_trace(ui, 6u, 0u, fx_native_ui_order_page(ui, page)) != FX_OK) repaint = 0;
            if (repaint && fx_native_ui_trace(ui, 7u, 0u, fx_native_row_reflow(&ui_layout, page)) != FX_OK) repaint = 0;
        }
        uint32_t repaint_rows = fx_native_ui_repaint_rows(ui, page, changed);
        for (uint32_t i = 0; i < d->count && repaint; ++i) {
            uint32_t row_id = rows[i].word[6];
            int enabled = 1;
            if (!(repaint_rows & (1u << i))) continue;
            uint32_t value_id = fx_native_ui_value(ui, row_id, &enabled);
            if (fx_native_ui_trace(ui, 8u, row_id, fx_native_text_prepare(&ui_text_profile, page, row_id,
                    rows[i].word[0], value_id, enabled, ui)) != FX_OK) {
                repaint = 0;
                continue;
            }
            void *row = FN(FX_ZF_ROW_LOOKUP, void *(*)(uint32_t, void *))(row_id, fx_native_page_rows(page));
            if (row) {
                FN(FX_ZF_TITLE_MARK_DIRTY, void (*)(void *, uint32_t))(row, 1u);
                void *value = FN(FX_ZF_CHILD_LOOKUP, void *(*)(void *, uint32_t))(row, FX_ZF_VALUE_CHILD);
                if (value) FN(FX_ZF_TITLE_MARK_DIRTY, void (*)(void *, uint32_t))(value, 1u);
                painted = 1u;
            }
        }
        if (repaint && painted) {
            /* restore is a core hook pointer, not an image window. The
             * signature is FxResult (*)(const FxNativePage *); it is stored in
             * a uintptr_t field, so it is cast through the function type. */
            uintptr_t restore = (uintptr_t)(FxResult (*)(const FxNativePage *))fx_native_backdrop_restore;
            FxNativeMenuDrawProfile draw = {FX_ZF_DRAW_DIRTY, FX_ZF_DRAW_DISPATCH, FX_ZF_DRAW_DEVICE,
                                            FX_ZF_DRAW_RECTANGLE, FX_ZF_DRAW_EMPTY, FX_ZF_DRAW_LOOKUP,
                                            FX_ZF_MODAL_ID, restore};
            if (changed & FX_NATIVE_UI_STRUCTURE)
                draw.restore = (uintptr_t)(FxResult (*)(const FxNativePage *))fx_native_menu_restore;
            if (fx_native_ui_trace(ui, 9u, 0u, fx_native_menu_draw(&draw, page)) != FX_OK) repaint = 0;
        }
    }
    uint32_t revision = repaint ? ui->revision : 0u;
    /* Match the Zf native callers: predicate callback, channel 3, flags 7. */
    if (repaint && painted) FN(FX_ZF_COMMIT_SUBMIT, void (*)(void *, uint32_t, uint32_t))
        ((void *)(uintptr_t)FX_ZF_COMMIT_OBJECT, 3u, 7u);
    if (repaint) (void)fx_native_ui_trace(ui, 10u, 0u, fx_native_ui_presented(ui, revision));
    FN(FX_ZF_GUI_LEAVE, void (*)(void *))(exclusion);
    if (repaint) return FX_OK;
    if (refreshed != FX_OK) return refreshed;
    return ui && ui->refresh_result != FX_OK ? ui->refresh_result : FX_BUSY;
}
