#include "fenestra_memory.h"
#include "fenestra_native_menu.h"
_Static_assert(sizeof(FxNativeRow) == 44 && sizeof(FxNativeEvent) == 32, "native row/event ABI");
_Static_assert(sizeof(FxNativePage) == 96 && sizeof(FxNativeDescriptor) == 64, "native page/descriptor ABI");
FxResult fx_native_menu_link(FxNativeEvent out[FX_NATIVE_EVENTS],const FxNativeEvent *source,
                             uint32_t terminal,const uint32_t select_events[4],
                             uintptr_t action,uint64_t next) {
    if(!out || !source || !select_events || !action)return FX_ARGUMENT;
    uint32_t n=0;
    while(n<FX_NATIVE_EVENTS && (source[n].event&65535u)!=terminal)++n;
    if(n==FX_NATIVE_EVENTS)return FX_INTEGRITY;
    memcpy(out,source,(n+1u)*sizeof(*out));
    for(uint32_t i=0;i<n;++i)
        for(uint32_t k=0;k<4u;++k)
            if((out[i].event&65535u)==select_events[k]) {
                out[i].callback=action;out[i].condition=0;out[i].next=next;
            }
    return FX_OK;
}
void fx_native_page_initialize(FxNativePage *out,const FxNativePage *source,uint32_t identity,
                               uintptr_t descriptor,uintptr_t callbacks[5],int child) {
    *out=*source;
    out->identity=identity;
    out->previous=child?(uintptr_t)source:source->previous;
    out->next=out->surface=out->content=0;
    out->count=out->selected=out->scroll=out->other=0;
    memset(out->unknown,0,sizeof(out->unknown));
    out->tail=0;
    if(source->callbacks)callbacks[2]=((const uintptr_t *)source->callbacks)[2];
    out->callbacks=(uintptr_t)callbacks;
    out->descriptor=descriptor;
}
FxResult fx_native_menu_build(FxNativeMenu *out, const FxNativeMenuProfile *p, uint32_t page, uint32_t title,
                              const FxNativeEntry *entries, uint32_t count, const uintptr_t callbacks[5]) {
    return fx_native_menu_build_style(out,p,page,title,entries,count,callbacks,0);
}
FxResult fx_native_menu_build_style(FxNativeMenu *out, const FxNativeMenuProfile *p, uint32_t page, uint32_t title,
                              const FxNativeEntry *entries, uint32_t count, const uintptr_t callbacks[5],
                              const FxNativeDialogStyle *style) {
    if (!out || !p || !p->page || !p->row || !p->navigation || !entries || !callbacks || !count ||
        count > FX_NATIVE_ROWS || !page || page > 65535u)
        return FX_ARGUMENT;
    uint32_t n = 0;
    while (n < FX_NATIVE_EVENTS && (p->navigation[n].event & 65535u) != p->terminal)
        ++n;
    if (n == FX_NATIVE_EVENTS)
        return FX_INTEGRITY;
    if(style && (!style->descriptor || !style->rows || style->row_count<count ||
                 style->header_bytes<272u || style->header_bytes>sizeof(out->header)))return FX_ARGUMENT;
    const FxNativeDescriptor *source = style?style->descriptor:(const FxNativeDescriptor *)p->page->descriptor;
    if (!source || !source->header)
        return FX_ARGUMENT;
    for (uint32_t i = 0; i < count; ++i)
        if (!entries[i].action || entries[i].submenu > 1u || entries[i].value > 1u)
            return FX_ARGUMENT;
    for (uint32_t i = 0; i < 5u; ++i)
        if (!callbacks[i])
            return FX_ARGUMENT;
    if (out->page.identity)
        return FX_BUSY;
    memcpy(out->callbacks, callbacks, sizeof(out->callbacks));
    fx_native_page_initialize(&out->page,p->page,(page<<16)|(p->page->identity&65535u),
                              (uintptr_t)&out->descriptor,out->callbacks,style!=0);
    out->descriptor = *source;
    memset(out->header,0,sizeof(out->header));
    memcpy(out->header, (const void *)source->header, style?style->header_bytes:272u);
    memcpy(out->header + 68u, &title, sizeof(title));
    out->descriptor.header = (uintptr_t)out->header;
    out->descriptor.rows = (uintptr_t)out->rows;
    out->descriptor.events = (uintptr_t)out->events;
    out->descriptor.count = (uint16_t)count;
    for (uint32_t r = 0; r < count; ++r) {
        const FxNativeEntry *e = &entries[r];
        out->rows[r] = style?style->rows[r]:*p->row;
        out->rows[r].word[0] = e->text;
        out->rows[r].word[2] = 65535u;
        out->rows[r].word[6] = e->row;
        out->rows[r].word[4] =
            (out->rows[r].word[4] & ~UINT32_C(0x41)) | (e->submenu ? 0x40u : 0u) | e->value;
        memcpy(out->handlers[r], p->navigation, (n + 1u) * sizeof(FxNativeEvent));
        for (uint32_t j = 0; j <= n; ++j) {
            uint32_t event = (uint32_t)p->navigation[j].event & 65535u;
            for (uint32_t k = 0; k < 4u; ++k)
                if (event == p->select_events[k]) {
                    out->handlers[r][j].callback = e->action;
                    out->handlers[r][j].condition = 0;
                    out->handlers[r][j].next = e->next;
                }
            if (event == p->back_event) {
                out->handlers[r][j].condition = 0;
                out->handlers[r][j].next = p->back;
            }
        }
        out->events[r] = (uintptr_t)out->handlers[r];
    }
    return FX_OK;
}

FxResult fx_native_menu_reader_layout(FxNativeMenu *menu,uint16_t width,uint16_t height) {
    if(!menu || width<360u || height<360u || width>32767u || height>32767u ||
       menu->page.surface || menu->page.content || menu->descriptor.header!=(uintptr_t)menu->header ||
       menu->descriptor.rows!=(uintptr_t)menu->rows || menu->descriptor.count!=2u ||
       (menu->descriptor.flags&UINT32_C(0x800000ff))!=UINT32_C(0x80000008))return FX_ARGUMENT;
    uint32_t background=0,text=0;
    memcpy(&background,menu->header+4u,4u);memcpy(&text,menu->header+72u,4u);
    if(background!=30u || text!=31u)return FX_INTEGRITY;
    /* Both original parsers use 68-byte records and two orientation rectangles.
     * Keep colours/fonts/resources; omit the compact warning icon/decorations. */
    const uint16_t outer[4]={0u,0u,width,height};
    const uint16_t body[4]={24u,24u,(uint16_t)(width-24u),(uint16_t)(height-140u)};
    for(uint32_t orientation=0;orientation<2u;++orientation) {
        memcpy(menu->header+24u+orientation*8u,outer,sizeof(outer));
        memcpy(menu->header+92u+orientation*8u,body,sizeof(body));
    }
    memset(menu->header+136u,0,sizeof(menu->header)-136u);
    /* Button row records encode x/y origins for each orientation. Preserve
     * the native x and widget style; reserve the bottom 118 pixels for both. */
    for(uint32_t row=0;row<2u;++row)
        for(uint32_t orientation=0;orientation<2u;++orientation) {
            uint32_t *xy=&menu->rows[row].word[8u+orientation];
            *xy=(*xy&65535u)|((uint32_t)(height-118u+row*58u)<<16);
        }
    return FX_OK;
}

static FxResult draw_pair(const FxNativeMenuDrawProfile *p,const FxNativePage *page) {
    if(!p || !page || !p->dirty || !p->dispatch || !p->device || !p->rectangle || !p->empty)return FX_ARGUMENT;
    void *surface=(void *)page->surface,*content=(void *)page->content;
    if(surface)((void (*)(void *,uint32_t))p->dirty)(surface,1u);
    if(content)((void (*)(void *,uint32_t))p->dirty)(content,1u);
    /* Nikon ignores each dispatch return here; it is not a pair-level ACK. */
    if(surface)(void)((uint32_t (*)(void *,void *,void *))p->dispatch)(surface,0,0);
    int16_t clip[4]={0,0,0,0};
    uint32_t device=((uint32_t (*)(uint32_t))p->device)(2u);
    ((void (*)(uint32_t,uint32_t,uint32_t,void *))p->rectangle)(1u,1u,device,clip);
    if(!((uint32_t (*)(const void *))p->empty)(clip) && content)
        (void)((uint32_t (*)(void *,void *,void *))p->dispatch)(content,clip,0);
    return FX_OK;
}

FxResult fx_native_menu_draw(const FxNativeMenuDrawProfile *p,const FxNativePage *page) {
    if(!p || !page)return FX_ARGUMENT;
    if(!fx_native_page_modal(page)) {
        if(p->restore) {
            FxResult restored=((FxResult (*)(const FxNativePage *))p->restore)(page);
            if(restored!=FX_EMPTY)return restored;
        }
        /* A keyboard shares the menu root's target. Returning to a retained
         * leaf must also restore its surface ancestors (sidebar/background).
         * previous is native surface ancestry, not the logical Back route. */
        const FxNativePage *chain[32];uint32_t count=0u;
        for(const FxNativePage *at=page;at;at=(const FxNativePage *)at->previous) {
            if(count==32u)return FX_INTEGRITY;
            for(uint32_t i=0u;i<count;++i)if(chain[i]==at)return FX_INTEGRITY;
            chain[count++]=at;
        }
        while(count) {
            FxResult result=draw_pair(p,chain[--count]);if(result!=FX_OK)return result;
        }
        return FX_OK;
    }
    if(!p->lookup || !p->modal_id || !page->surface)return FX_ARGUMENT;
    void *body=((void *(*)(uint32_t,void *))p->lookup)(p->modal_id,(void *)page->surface);
    if(!body)return FX_STATE;
    if(p->restore) {
        FxResult restored=((FxResult (*)(const FxNativePage *))p->restore)(page);
        if(restored==FX_OK)return draw_pair(p,page);
        if(restored!=FX_EMPTY)return restored;
    }
    /* Both native body renderers use +0xce as full-screen shade alpha
     * (C110 40335440 / C532 4032939c). The existing backdrop is already
     * shaded. Suppress a second composition during this redraw only.
     * Keep the original container dispatcher: it clears child dirty bits
     * after painting (C110 40331eec / C532 40325f44). A direct body call
     * leaves it dirty and causes another shade on the next original event. */
    uint8_t *shade=(uint8_t *)body+0xceu;uint8_t saved=*shade;*shade=0;
    FxResult result=draw_pair(p,page);*shade=saved;return result;
}
