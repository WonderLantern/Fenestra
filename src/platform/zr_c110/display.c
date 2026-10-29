/* Nikon ZR C1.10 GUI/render service binding. No MMIO, physical address
 * arithmetic, emulator register layout or owned scanout buffer is used.
 * The base-object/list/paint ABI is derived from this firmware's own menu
 * callers (403bc830..403bc8c4, 40331468..40331574, 40400358..403e8).
 * This is an internal, version-pinned ABI; it is not a public Nikon SDK. */
#include "fenestra_native_menu.h"
#include "fenestra_c110_display.h"
#include "fenestra_memory.h"
#include "fenestra_native_text.h"
#include "fenestra_native_ui_callbacks.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#define GUI_LOCK UINT64_C(0xa034e7200)
static void lock(void) { FN(UINT64_C(0x40f40c30),void (*)(void *))((void *)GUI_LOCK); }
static void unlock(void) { FN(UINT64_C(0x40f40bdc),void (*)(void *))((void *)GUI_LOCK); }
static int native_ram(const void *p,uint32_t bytes) {
    uintptr_t a=(uintptr_t)p;
    return !(a&7u) && a>=UINT64_C(0xa00000000) && a<=UINT64_C(0xa40000000)-bytes;
}
static void invalidate(void *object,uint32_t depth,uint32_t *budget) {
    if(!*budget || depth>8u || !native_ram(object,0xc0u)) return;
    --*budget;
    FN(UINT64_C(0x40401060),void (*)(void *,uint32_t))(object,1u);
    uintptr_t callback=*(uintptr_t *)((uint8_t *)object+0x48u);
    uint32_t offset,wrapped;
    if(callback==UINT64_C(0x40331e74)) { offset=0xc8u;wrapped=0u; }
    else if(callback==UINT64_C(0x4032fd24) || callback==UINT64_C(0x403353f0)) {
        offset=0xd8u;wrapped=1u;
    } else if(callback==UINT64_C(0x4032b258)) {
        /* Native row widgets contain cached image/text children, including
         * sidebar icons. Marking only the row cannot repaint those children. */
        offset=0x130u;wrapped=0u;
    } else return;
    if(!native_ram(object,offset+32u)) return;
    void *list=(uint8_t *)object+offset;
    uint32_t count=FN(UINT64_C(0x40e7aaa4),uint32_t (*)(void *))(list);
    if(count>256u) return;
    void *cursor=FN(UINT64_C(0x40e7ae54),void *(*)(void *))(list);
    for(uint32_t i=0u;i<count && *budget;++i) {
        void *child=FN(UINT64_C(0x40e7ac10),void *(*)(void *,void **))(list,&cursor);
        if(wrapped) {
            if(!native_ram(child,8u)) return;
            child=*(void **)child;
        }
        invalidate(child,depth+1u,budget);
    }
}
static const FxNativeTextProfile ui_text_profile={
    0x403e8e8cu,0x40400330u,0x404012d0u,0x40401090u,0x404010d0u,
    0x40337eecu,0x4032b258u,0x4fb1u,0x148u,0x120u,0x404012a4u,0x4032a618u,0x4032a4f4u};
static FxResult update_menu_text_locked(void *page,uint32_t row_id,uint32_t text_id,int repaint) {
    if(!native_ram(page,96u)) return FX_STATE;
    if(FN(UINT64_C(0x403e8e8c),void *(*)(void))()!=page) { return FX_STATE; }
    void *content=fx_native_page_rows((FxNativePage *)page);
    if(!native_ram(content,0x118u)) { return FX_STATE; }
    /* Native row lookup and text-cache release; the next original draw reads
     * the current menu getter, rather than reusing the entry-time string. */
    void *row=FN(UINT64_C(0x40400330),void *(*)(uint32_t,void *))(row_id,content);
    if(!native_ram(row,0x148u) || *(uintptr_t *)((uint8_t *)row+0x48u)!=UINT64_C(0x4032b258)) {
        return FX_STATE;
    }
    void *list=(uint8_t *)row+0x130u;
    uint32_t count=FN(UINT64_C(0x40e7aaa4),uint32_t (*)(void *))(list);
    if(count>32u) { return FX_BOUNDS; }
    void *cursor=FN(UINT64_C(0x40e7ae54),void *(*)(void *))(list);
    uint32_t found=0u;
    FxNativeUi *ui=fx_native_ui_find_page(((FxNativePage *)page)->identity>>16);
    int enabled=1;uint32_t value_id=ui?fx_native_ui_value(ui,row_id,&enabled):fx_c110_menu_row_state(row_id,&enabled);
    FxResult prepared=fx_native_ui_trace(ui,8u,row_id,fx_native_text_prepare(&ui_text_profile,page,row_id,text_id,value_id,enabled,ui));
    if(prepared!=FX_OK)return prepared;
    FN(UINT64_C(0x40401060),void (*)(void *,uint32_t))(row,1u);
    void *value=value_id?FN(UINT64_C(0x404012d0),void *(*)(void *,uint32_t))(row,0x4fb1u):0;
    if(native_ram(value,0x120u)) {
        FN(UINT64_C(0x40401060),void (*)(void *,uint32_t))(value,1u);
        ++found;
    }
    uint32_t *row_words=row;
    if(row_words[2]==text_id && row_words[3]==2u)++found;
    for(uint32_t i=0u;i<count;++i) {
        void *child=FN(UINT64_C(0x40e7ac10),void *(*)(void *,void **))(list,&cursor);
        if(!native_ram(child,0x80u)) { return FX_STATE; }
        uint32_t *words=child;
        if(words[2]==text_id && words[3]==2u) {
            (void)FN(UINT64_C(0x40337eec),uint32_t (*)(void *))(child);
            FN(UINT64_C(0x40401060),void (*)(void *,uint32_t))(child,1u);
            ++found;
        }
    }
    if(found && repaint) {
        uint32_t budget=1024u;
        void *surface=*(void **)((uint8_t *)page+24u);
        if(native_ram(surface,0x118u)) {
            invalidate(surface,0u,&budget);
            (void)FN(UINT64_C(0x40332694),uint32_t (*)(void *,void *,void *))(surface,0,0);
        }
        invalidate(content,0u,&budget);
        (void)FN(UINT64_C(0x40332694),uint32_t (*)(void *,void *,void *))(content,0,0);
    }
    return found?FX_OK:FX_STATE;
}
static FxResult update_menu_text(void *page,uint32_t row,uint32_t text,int repaint) {
    lock();FxResult result=update_menu_text_locked(page,row,text,repaint);
    if(result==FX_OK && repaint)FN(UINT64_C(0x403ff6b0),void (*)(void *,uint32_t,uint32_t))
        ((void *)(uintptr_t)UINT64_C(0x403ff64c),3u,7u);
    unlock();
    return result;
}
FxResult fx_c110_display_refresh_text(void *page,uint32_t row,uint32_t text) {
    return update_menu_text(page,row,text,1);
}
FxResult fx_c110_display_prepare_text(void *page,uint32_t row,uint32_t text) {
    /* The native open callback precedes row placement. Let Nikon finish layout
     * and perform its first draw; painting here puts every row at the origin. */
    return update_menu_text(page,row,text,0);
}
FxResult fx_c110_display_prepare_layout(void *page,const FxNativeLayoutProfile *profile) {
    lock();FxResult result=fx_native_row_reflow(profile,page);unlock();return result;
}
FxResult fx_c110_display_refresh_page(void *context,FxResult (*prepare)(void *,void *,uint32_t *),FxResult (*presented)(void *)) {
    if(!prepare)return FX_ARGUMENT;
    lock();void *page=FN(UINT64_C(0x403e8e8c),void *(*)(void))();
    uint32_t changed=0u;
    FxResult result=native_ram(page,96u)?prepare(context,page,&changed):FX_EMPTY;
    if(result==FX_OK) {
        const uint8_t *descriptor=*(const uint8_t **)((uint8_t *)page+80u);
        const uint32_t *rows=*(const uint32_t *const *)(descriptor+16u);
        uint32_t count=*(const uint16_t *)(descriptor+32u);
        if(count>18u)result=FX_BOUNDS;
        else {
            for(uint32_t i=0;i<count;++i) {
                if(!(changed&(1u<<i)))continue;
                FxResult row=update_menu_text_locked(page,rows[i*11u+6u],rows[i*11u],0);
                if(row!=FX_OK)result=row;
            }
            /* Original leaf refresh403bee30 first dirties both current-page
             * canvases (403a8770/40406418), then draws them. Both buffers must
             * receive unchanged rows/chrome too. Never dirty/redraw unrelated
             * ancestors, and use the lock-free dispatch under our GUI lock. */
            if(result==FX_OK && changed) {
                FxNativeMenuDrawProfile draw={0x40406418u,0x40332694u,0x404005a0u,0x40404684u,0x40e7c614u,0x40400330u,0x4fb7u,(uintptr_t)fx_native_backdrop_restore};
                if(changed&FX_NATIVE_UI_STRUCTURE)draw.restore=(uintptr_t)fx_native_menu_restore;
                result=fx_native_menu_draw(&draw,page);
            }
        }
    }
    /* Dispatch binds each original target. Keep that binding and compositor
     * submission in one GUI transaction: another task must not switch the
     * native target between drawing and publishing the frame. */
    if(result==FX_OK && changed)FN(UINT64_C(0x403ff6b0),void (*)(void *,uint32_t,uint32_t))
        ((void *)(uintptr_t)UINT64_C(0x403ff64c),3u,7u);
    if(result==FX_OK && presented)result=presented(context);
    unlock();
    return result;
}

/* Compatibility entry points for existing C1.10 UI/Panic qualification tools. */
FxC110View fx_c110_current_view(void){return fx_native_current_view();}
FxResult fx_c110_display_attach_global(FxC110Display *d,const FxDemoScene *s){return fx_native_display_attach_global(d,s);}
FxResult fx_c110_display_attach(FxC110Display *d,void *p,const FxDemoScene *s){return fx_native_display_attach(d,p,s);}
FxResult fx_c110_display_present(FxC110Display *d,const FxDemoScene *s){return fx_native_display_present(d,s);}
void fx_c110_display_detach(FxC110Display *d){fx_native_display_detach(d);}
void *fx_c110_display_current_page(uint32_t id){return fx_native_display_current_page(id);}
