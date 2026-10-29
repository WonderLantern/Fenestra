#include "fenestra_native_visibility.h"
static int aligned_ram(const void *p,uint32_t n,uintptr_t alignment) {
    uintptr_t a=(uintptr_t)p;
    return p && !(a&(alignment-1u)) && a>=UINT64_C(0xa00000000) && a<=UINT64_C(0xa40000000)-n;
}
static int ram(const void *p,uint32_t n) {return aligned_ram(p,n,8u);}
uint32_t fx_native_row_draw_guard(void *row,void *clip,void *progress) {
    (void)clip; /* Like the original row callback; its parent clips the row. */
    if(!row)return 0u;
    if(!*((const uint8_t *)row+0x28u))return 1u;
    uintptr_t draw=*(const uintptr_t *)((const uint8_t *)row+0x48u);
    return draw?((uint32_t (*)(void *,void *))draw)(row,progress):0u;
}
static FxResult find_row(const FxNativeVisibilityProfile *p,FxNativePage *page,uint32_t id,void **out) {
    if(!p || !ram(page,sizeof(*page)) || !p->current_page || !p->row_lookup ||
       !p->set_active || !p->row_draw || p->row_bytes<0x70u)return FX_ARGUMENT;
    if(((void *(*)(void))p->current_page)()!=page || !ram(fx_native_page_rows(page),0xd8u))return FX_STATE;
    void *row=((void *(*)(uint32_t,void *))p->row_lookup)(id,fx_native_page_rows(page));
    if(!ram(row,p->row_bytes) || *(uintptr_t *)((uint8_t *)row+0x48u)!=p->row_draw)return FX_STATE;
    *out=row;return FX_OK;
}
FxResult fx_native_row_visibility(const FxNativeVisibilityProfile *p,FxNativePage *page,uint32_t id,int visible) {
    void *row=0;FxResult r=find_row(p,page,id,&row);if(r!=FX_OK)return r;
    if(p->row_bytes>=0xb8u) {
        uintptr_t *extended=(uintptr_t *)((uint8_t *)row+0xb0u);
        if(!*extended)*extended=(uintptr_t)fx_native_row_draw_guard;
    }
    ((void (*)(void *,uint32_t))p->set_active)(row,visible?1u:0u);
    return FX_OK;
}
FxResult fx_native_row_arrow(const FxNativeVisibilityProfile *p,FxNativePage *page,uint32_t id,int submenu) {
    if(!p || !p->child_lookup || !p->arrow_child)return FX_ARGUMENT;
    void *row=0;FxResult r=find_row(p,page,id,&row);if(r!=FX_OK)return r;
    void *arrow=((void *(*)(void *,uint32_t))p->child_lookup)(row,p->arrow_child);
    if(!arrow)return submenu?FX_STATE:FX_OK;
    if(!ram(arrow,0x70u))return FX_STATE;
    ((void (*)(void *,uint32_t))p->set_active)(arrow,submenu?1u:0u);
    return FX_OK;
}
FxResult fx_native_row_value_visibility(const FxNativeVisibilityProfile *p,FxNativePage *page,uint32_t id,int visible) {
    if(!p || !p->child_lookup || !p->value_child)return FX_ARGUMENT;
    void *row=0;FxResult r=find_row(p,page,id,&row);if(r!=FX_OK)return r;
    void *value=((void *(*)(void *,uint32_t))p->child_lookup)(row,p->value_child);
    if(!value)return visible?FX_STATE:FX_OK;
    if(!ram(value,0x70u))return FX_STATE;
    ((void (*)(void *,uint32_t))p->set_active)(value,visible?1u:0u);
    return FX_OK;
}
FxResult fx_native_row_reflow(const FxNativeLayoutProfile *p,FxNativePage *page) {
    if(!p || !ram(page,sizeof(*page)) || !p->current_page || !p->count_rows ||
       !p->highlight || !p->reset_check || !p->repair_selection || !p->place_rows ||
       !p->resize_canvas || !p->scrollbar)return FX_ARGUMENT;
    if(((void *(*)(void))p->current_page)()!=page)return FX_STATE;
    const FxNativeDescriptor *d=(const FxNativeDescriptor *)page->descriptor;
    if(!ram(d,sizeof(*d)) || !d->count || d->count>FX_NATIVE_ROWS)return FX_STATE;
    uint32_t index=fx_native_page_row_index(page);
    if(index>=d->count)return FX_STATE;
    const FxNativeRow *rows=(const FxNativeRow *)d->rows;
    /* Rows contain uint32_t words: the real owned menu places them at +500.
     * Requiring object/pointer alignment rejects this valid native layout. */
    if(!aligned_ram(rows,d->count*(uint32_t)sizeof(*rows),_Alignof(FxNativeRow)))return FX_STATE;
    for(uint32_t i=0;i<d->count;++i) {
        ((void (*)(void *,uint32_t,uint32_t))p->highlight)(page,i,0u);
        ((void (*)(void *,uint32_t,uint32_t))p->reset_check)(page,rows[i].word[6],0u);
    }
    uint32_t count=((uint32_t (*)(void *,uint32_t))p->count_rows)(page,UINT32_C(0x50002));
    if(!count || count>d->count)return FX_STATE;
    page->count=(uint16_t)count;
    ((void (*)(void *))p->repair_selection)(page);
    ((void (*)(void *))p->place_rows)(page);
    ((void (*)(void *))p->resize_canvas)(page);
    if(p->prepare_viewport)((void (*)(void *))p->prepare_viewport)(page);
    ((void (*)(void *))p->scrollbar)(page);
    return FX_OK;
}
