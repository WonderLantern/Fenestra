#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_ui_editor.h"
#include "fenestra_native_modules.h"
#include "fenestra_native_ui_dispatch.h"
#ifdef FX_NATIVE_SCENE
#include "fenestra_native_scene.h"
#endif
/* Both immutable profiles verify the original condition caller: x20 is
 * its source page and x22 its selected FxNativeEvent. Condition callbacks
 * have no ordinary argument contract (x0 is an unrelated group value).
 * Preserve the caller's context; never infer an owner from current_page. */
#if defined(__aarch64__)
__asm__(".section .text.fx_native_ui_condition_gate,\"ax\"\n"
        ".balign 4\n.global fx_native_ui_condition_gate\n"
        ".type fx_native_ui_condition_gate,%function\n"
        "fx_native_ui_condition_gate:\n mov x0,x20\n mov x1,x22\n"
        " b fx_native_ui_condition_dispatch\n"
        ".size fx_native_ui_condition_gate,.-fx_native_ui_condition_gate\n.previous\n");
#else
uint32_t fx_native_ui_condition_gate(void){return 0u;} /* CPU adapter is AArch64 only. */
#endif
uint32_t fx_native_ui_condition_dispatch(const FxNativePage *page,const FxNativeEvent *event) {
    if(!page)return 0u;
    FxNativeUi *u=fx_native_ui_find_page(page->identity>>16);
    FxResult result=fx_native_ui_event(u,page,event);
    if(result==FX_OK && u->input_ready)u->input_ready(u);
    return result==FX_OK?1u:0u;
}
#define GATE(n) (uintptr_t)fx_native_ui_condition_gate,
const uintptr_t fx_native_ui_actions[16]={FX_EACH_UI_SLOT(GATE)};
const uintptr_t fx_native_ui_options[8]={FX_EACH_UI_OPTION(GATE)};
FxNativeUi *fx_native_ui_find_page(uint32_t page) {
    FxNativeUi *u=fx_ui_dispatch_find_page(fx_native_ui_dispatch_domain(),page);
    return u && u->source && u->source->resolve?u->source->resolve(u,page):u;
}
uint32_t fx_native_ui_page_open(void *page) {
    FxNativePage *p = page;
    if (!p)
        return 0u;
    uint32_t id = p->identity >> 16;
    FxNativeUi *u = fx_native_ui_find_page(id);
    if(u && fx_native_ui_page_matches(u,p)) {
        fx_native_ui_capture_task();
        (void)fx_native_ui_open(u, id);
#ifdef FX_NATIVE_SCENE
        (void)fx_native_backdrop_open(p);
#endif
        fx_native_ui_refresh_owner(u->owner);
    }
    return 1u;
}
uint32_t fx_native_ui_page_close(void *page) {
    FxNativePage *p = page;
    if (!p)
        return 0u;
    uint32_t id = p->identity >> 16;
    FxNativeUi *u = fx_native_ui_find_page(id);
    if(u && fx_native_ui_page_matches(u,p)) {
#ifdef FX_NATIVE_SCENE
        fx_native_scene_closed(u->owner,id);
        fx_native_backdrop_close(p);
#endif
        (void)fx_native_ui_close(u, id);
    }
    return 1u;
}
uint32_t fx_native_ui_noop(void *page) {
    (void)page;
    return 1u;
}

FxResult fx_native_ui_text_input(uint32_t control,const char *text,uint32_t bytes) {
    const FxNativePage *page=((const FxNativePage *(*)(void))fx_native_ui_dispatch_profile.current_page)();
    if(!page)return FX_STATE;
    uint32_t id=page->identity>>16;
    FxNativeUi *u=fx_native_ui_find_page(id);
    uint32_t slot=fx_native_ui_slot(u,id);
    if(!u || !u->owner || slot>=FX_NATIVE_UI_PAGES || !u->pinned[slot])return FX_STATE;
    if(fx_native_ui_editor_current(u)) {
        const FxNativePickerView *view=&u->editor->observed;
        if(view->control.id!=control)return FX_STALE;
        return fx_ui_route_text_observed(u->owner,&view->control,view->revision,text,bytes);
    }
    /* A staged publication is not the definition the user just edited. The
     * native input handler can keep its page open and retry after submission. */
    if(atomic_load_explicit(&u->presented_revision,memory_order_acquire)!=u->revision)return FX_BUSY;
    if(slot==FX_NATIVE_UI_PAGES-1u) {
        FxNativePickerView view;FxResult r=fx_native_ui_picker_snapshot(u,&view);
        if(r!=FX_OK)return r;
        if(view.control.id!=control)return FX_STALE;
        return fx_ui_route_text_observed(u->owner,&view.control,view.revision,text,bytes);
    }
    for(uint32_t i=0;i<u->count;++i)if(u->controls[i].id==control)
        return fx_ui_route_text_observed(u->owner,&u->controls[i],u->control_revisions[i],text,bytes);
    return FX_STALE;
}

FxNativeUi *fx_native_ui_editor_input_owner(void) {
    const FxNativePage *page=((const FxNativePage *(*)(void))fx_native_ui_dispatch_profile.current_page)();
    FxNativeUi *u=page?fx_native_ui_find_page(page->identity>>16):0;
    return fx_native_ui_editor_current(u) && page==&u->editor->native.page?u:0;
}
