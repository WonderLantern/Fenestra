#include "core_port_profiles.h"
#include "fenestra_native_title.h"

static int ram(const void *p,uint32_t bytes) {
    return fx_native_ram_contains(p,bytes,8u);
}
FxResult fx_native_title_object(const FxNativeTitleProfile *p,FxNativePage *page,void **out) {
    if(!out || !p || !ram(page,sizeof(*page)) || !p->current_page || !p->lookup ||
       !p->release_text || !p->mark_dirty || !p->destructor)return FX_ARGUMENT;
    if(((void *(*)(void))p->current_page)()!=page || !ram((void *)page->surface,0xe0u) ||
       !ram((void *)page->descriptor,sizeof(FxNativeDescriptor)))return FX_STATE;
    const FxNativeDescriptor *d=(const FxNativeDescriptor *)page->descriptor;
    if(!ram((void *)d->header,0x80u))return FX_STATE;
    const uint32_t *header=(const uint32_t *)d->header;
    uint32_t resource=header[0x44u/4u],id=header[0x7cu/4u];
    uintptr_t destructor=p->destructor;
    if(fx_native_page_modal(page)) {
        if(!p->modal_id || !p->modal_destructor)return FX_ARGUMENT;
        id=p->modal_id;destructor=p->modal_destructor;
    }
    void *title=((void *(*)(uint32_t,void *))p->lookup)(id,(void *)page->surface);
    if(!ram(title,0xd8u))return FX_STATE;
    const uint32_t *words=title;
    if(words[0]!=id || words[2]!=resource || words[3]!=2u ||
       *(const uintptr_t *)((const uint8_t *)title+0x58u)!=destructor)return FX_STATE;
    *out=title;return FX_OK;
}
FxResult fx_native_title_prepare(const FxNativeTitleProfile *p,FxNativePage *page) {
    void *title=0;FxResult r=fx_native_title_object(p,page,&title);if(r!=FX_OK)return r;
    /* The original helper releases its OWN shaped-text cache at +0x68, not
     * the module's copied UTF-16 resource. The next native draw resolves it. */
    (void)((uint32_t (*)(void *))p->release_text)(title);
    ((void (*)(void *,uint32_t))p->mark_dirty)(title,1u);
    return FX_OK;
}
