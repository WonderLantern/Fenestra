#include "fenestra_native_panic.h"
FxResult fx_native_panic_show(FxNativeDisplay *display,FxScene *scene,const FxPanicReport *report) {
    if(!display || !scene || !report)return FX_ARGUMENT;
    if(!scene->count) {
        FxResult r=fx_panic_scene(scene,report);if(r!=FX_OK)return r;
    }
    display->exact_colors=1u;
    if(atomic_load_explicit(&display->attached,memory_order_acquire) &&
       !fx_native_display_is_current(display)) {
        /* An action can close its native modal page after the first fault
         * frame. The retained error must follow the actual current target. */
        fx_native_display_detach(display);
        if(atomic_load_explicit(&display->attached,memory_order_acquire))return FX_BUSY;
    }
    if(!atomic_load_explicit(&display->attached,memory_order_acquire)) {
        FxNativeView view=fx_native_current_view();
        FxResult r;
        if(view.id==fx_native_display_profile.shooting_view_id && view.state==1u)
            r=fx_native_display_attach_global(display,scene);
        else {
            void *page=((void *(*)(void))fx_native_display_profile.native_current_page)();
            if(!page)return FX_BUSY;
            r=fx_native_display_attach(display,page,scene);
        }
        if(r!=FX_OK)return r;
    }
    return fx_native_display_present(display,scene);
}
FxResult fx_native_panic_poll(FxNativePanicPage *page) {
    if(!page || !page->captured)return FX_STATE;
    if(page->shown && fx_native_display_is_current(&page->display))return FX_OK;
    FxResult r=fx_native_panic_show(&page->display,&page->scene,&page->report);
    page->result=(uint32_t)r;page->shown=r==FX_OK;
    return r;
}
