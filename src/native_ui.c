#include "fenestra_memory.h"
#include "fenestra_native_ui.h"
#include "fenestra_native_ui_editor.h"
#include "fenestra_native_ui_reader.h"
#include "fenestra_native_ui_callbacks.h"
void fx_native_ui_account_retained(FxNativeUi *u) {
    if(!u)return;
    for(uint32_t i=0u;i<FX_NATIVE_UI_PAGES;++i)
        if(atomic_load_explicit(&u->pages[i],memory_order_seq_cst))fx_native_ui_keep(u,&u->page_accounted[i],(uint32_t)sizeof(FxNativeMenu));
    if(atomic_load_explicit(&u->editor,memory_order_seq_cst))fx_native_ui_keep(u,&u->editor_accounted,(uint32_t)sizeof(FxNativeUiEditor));
    if(atomic_load_explicit(&u->reader,memory_order_seq_cst))fx_native_ui_keep(u,&u->reader_accounted,(uint32_t)sizeof(FxNativeUiReader));
}
/* Own layout, for read-only external validation of actual native row/event
 * identity. No firmware runtime path consumes this observation table. */
const uint32_t fx_native_ui_topology_metadata[10]={0x46585554u,2u,
    offsetof(FxNativeUi,page_base),offsetof(FxNativeUi,row_base),offsetof(FxNativeUi,order),
    offsetof(FxNativeUi,layout_revisions),offsetof(FxPlatformOwner,ui_order),
    offsetof(FxNativeUi,actions),offsetof(FxNativeUi,profile)+offsetof(FxNativeMenuProfile,select_events),FX_UI_CONTROLS};
/* Text was bounded/validated at publication. Decode UTF-8 into owned native
 * UTF-16 storage; never pass module pointers to Nikon's GUI. */
static void utf16(uint16_t out[FX_UI_TEXT], const char *in) {
    uint32_t i = 0, n = 0;
    while (in[i] && n < FX_UI_TEXT - 1u) {
        uint32_t c = (uint8_t)in[i++], extra = 0;
        if (c >= 0xf0u) {
            c &= 7u;
            extra = 3u;
        } else if (c >= 0xe0u) {
            c &= 15u;
            extra = 2u;
        } else if (c >= 0xc0u) {
            c &= 31u;
            extra = 1u;
        }
        for (uint32_t j = 0; j < extra; ++j)
            c = (c << 6) | ((uint8_t)in[i++] & 63u);
        if (c >= 0x10000u) {
            c -= 0x10000u;
            out[n++] = (uint16_t)(0xd800u + (c >> 10));
            out[n++] = (uint16_t)(0xdc00u + (c & 1023u));
        } else
            out[n++] = (uint16_t)c;
    }
    out[n] = 0;
}
static FxResult copied_layout(FxNativeUi *u,FxUiControl *out,uint32_t *count,uint32_t *revision,uint32_t *versions,uint8_t *order) {
    return u->source && u->source->snapshot?u->source->snapshot(u,out,count,revision,versions,order):
        fx_ui_snapshot_layout(u->owner,out,FX_UI_CONTROLS,count,revision,versions,order);
}
int fx_native_ui_page_matches(const FxNativeUi *u,const FxNativePage *page) {
    if(!u || !page)return 0;
    uint32_t slot=fx_native_ui_slot(u,page->identity>>16);
    if(slot>=FX_NATIVE_UI_PAGES)return 0;
    if(slot==FX_NATIVE_UI_PAGES-1u && fx_native_ui_editor_current(u))
        return page->descriptor==u->editor->native.page.descriptor;
    return u->pages[slot] && page->descriptor==(uintptr_t)&u->pages[slot]->descriptor;
}
static FxResult snapshot(FxNativeUi *u) {
    FxResult r;
    if(u->source && u->count) {
        uint32_t changed=0u;r=fx_native_ui_refresh_values(u,&changed);
        return r==FX_EMPTY?FX_OK:r;
    }
    if(u->source) {
        uint32_t count=0u,revision=0u;
        r=copied_layout(u,u->pending,&count,&revision,u->pending_control_revisions,u->pending_order);
        if(r!=FX_OK)return r;
        memcpy(u->controls,u->pending,count*sizeof(*u->controls));
        memcpy(u->control_revisions,u->pending_control_revisions,count*sizeof(*u->control_revisions));
        memcpy(u->order,u->pending_order,count);u->count=count;u->revision=revision;
    } else r=copied_layout(u,u->controls,&u->count,&u->revision,u->control_revisions,u->order);
    if (r != FX_OK)
        return r;
    u->root_control=0u;
    for (uint32_t i = 0; i < u->count; ++i) {
        const FxUiControl *c = &u->controls[i];
        if((c->flags&(FX_UI_ROOT|FX_UI_RETIRED))==FX_UI_ROOT)u->root_control=c->id;
        utf16(u->labels[i], c->label);
        const char *value = c->text;
        if(c->kind==FX_UI_DIALOG && !*value)value=c->label;
        if (c->kind == FX_UI_SWITCH)
            value = c->value ? "On" : "Off";
        if (c->kind == FX_UI_CHOICE)
            value = c->options[c->value];
        utf16(u->values[i], value);
        memcpy(u->row_labels[i],u->labels[i],sizeof(u->labels[i]));
        memcpy(u->row_values[i],u->values[i],sizeof(u->values[i]));
    }
    return FX_OK;
}
static uint32_t picker_count(const FxUiControl *c) {
    return c->kind==FX_UI_CHOICE?c->option_count:
        (c->kind==FX_UI_SWITCH || c->kind==FX_UI_DIALOG || c->kind==FX_UI_DOCUMENT)?2u:0u;
}
static FxResult picker_stage(FxNativeUi *u,const FxUiControl *c,uint32_t revision,uint32_t ui_revision) {
    if(!revision || !picker_count(c))return FX_STATE;
    uint64_t active=atomic_load(&u->picker_active);
    if(active && u->picker_views[active&1u].revision==revision &&
       !memcmp(&u->picker_views[active&1u].control,c,sizeof(*c))) {
        u->picker_staged=0u;return FX_OK;
    }
    uint32_t bank=active?((uint32_t)active&1u)^1u:0u;
    if(atomic_load(&u->picker_readers[bank]))return FX_BUSY;
    if(active>UINT64_MAX-2u)return FX_BUDGET;
    u->picker_views[bank]=(FxNativePickerView){.control=*c,.revision=revision};
    u->picker_staged=((active&~UINT64_C(1))+2u)|bank;
    u->picker_staged_revision=ui_revision;
    return FX_OK;
}
static void picker_publish(FxNativeUi *u) {
    if(u->picker_staged) {
        atomic_store(&u->picker_active,u->picker_staged);
        u->picker_staged=0u;
    }
}
FxResult fx_native_ui_picker_snapshot(FxNativeUi *u,FxNativePickerView *out) {
    if(!u || !u->owner || !out)return FX_ARGUMENT;
    for(uint32_t attempt=0;attempt<4u;++attempt) {
        uint64_t active=atomic_load(&u->picker_active);if(!active)return FX_STATE;
        uint32_t bank=(uint32_t)active&1u;
        atomic_fetch_add(&u->picker_readers[bank],1u);
        if(atomic_load(&u->picker_active)==active) {
            *out=u->picker_views[bank];
            atomic_fetch_sub(&u->picker_readers[bank],1u);return FX_OK;
        }
        atomic_fetch_sub(&u->picker_readers[bank],1u);
    }
    atomic_fetch_add(&u->owner->dropped_inputs,1u);return FX_BUSY;
}
static uint32_t picker_labels(FxNativeUi *u,const FxUiControl *c) {
    uint32_t changed=0u,count=picker_count(c);
    for(uint32_t i=0;i<FX_UI_OPTIONS;++i) {
        const char *value=i>=count?"":c->kind==FX_UI_CHOICE?c->options[i]:
            c->kind==FX_UI_SWITCH?(i?"On":"Off"):c->kind==FX_UI_DOCUMENT?(i?"Next":"Previous"):(i?"Cancel":"OK");
        uint16_t next[FX_UI_TEXT]={0};utf16(next,value);
        if(memcmp(next,u->option_text[i],sizeof(next)))changed|=1u<<(FX_UI_CONTROLS+i);
        memcpy(u->option_text[i],next,sizeof(next));
    }
    return changed;
}
FxResult fx_native_ui_presented(FxNativeUi *u,uint32_t revision) {
    if(!u || !u->owner || revision!=u->revision)return FX_STALE;
    if(u->picker_staged && u->picker_staged_revision!=revision)return FX_STALE;
    picker_publish(u);
    atomic_store_explicit(&u->presented_revision,revision,memory_order_release);
    uint64_t focus=atomic_load_explicit(&u->owner->ui_focus,memory_order_acquire);
    atomic_store_explicit(&u->owner->ui_submission,((uint64_t)revision<<32)|(focus>>33),memory_order_release);
    if(u->projection_pending==2u && !u->pinned[FX_NATIVE_UI_PAGES-1u]) {
        u->projection_pending=0u;u->return_focus=0u;
    }
    u->recovery_dirty=u->recovery_focus=u->recovery_restore=0u;
    return FX_OK;
}
FxResult fx_native_ui_init(FxNativeUi *u, FxPlatformOwner *owner, const FxNativeMenuProfile *profile,
                           void *(*allocate)(uint32_t), uint32_t page, uint32_t text, uint32_t row,
                           const uintptr_t actions[FX_UI_CONTROLS], const uintptr_t options[FX_UI_OPTIONS],
                           const uintptr_t callbacks[5]) {
    if (!u || !owner || !profile || !allocate || !actions || !options || !callbacks || !page ||
        page > 65535u - FX_NATIVE_UI_PAGES || row > 65535u - 41u || text > UINT32_MAX - FX_NATIVE_UI_TEXTS)
        return FX_ARGUMENT;
    if (u->owner)
        return FX_BUSY;
    memset(u, 0, sizeof(*u));
    u->owner = owner;
    u->profile = *profile;
    u->allocate = allocate;
    u->observer=fx_platform_account_observer(owner,"ui.native");
    u->page_base = page;
    u->text_base = text;
    u->row_base = row;
    u->editing = UINT32_MAX;
    memcpy(u->actions, actions, sizeof(u->actions));
    memcpy(u->options, options, sizeof(u->options));
    memcpy(u->callbacks, callbacks, sizeof(u->callbacks));
    return FX_OK;
}
/* One construction frame regardless of parent depth. Native task stacks must
 * not carry a full row/event staging array for every logical ancestor. */
static FxNativePage *build_page(FxNativeUi *u,uint32_t slot,const FxNativePage *parent) {
    if(slot==FX_NATIVE_UI_PAGES-1u && u->editing<u->count && u->controls[u->editing].kind==FX_UI_EDIT)
        return fx_native_ui_editor_page(u,parent);
    if(slot==FX_NATIVE_UI_PAGES-1u && u->editing<u->count && u->controls[u->editing].kind==FX_UI_DOCUMENT &&
       fx_native_ui_reader_begin(u)!=FX_OK)return 0;
    FxNativeMenu *menu=u->pages[slot];
    /* Native choice pages return to the same control. Preserve its stable
     * row identity across a rebuild; a hidden preceding row can change its
     * index, so retaining only selected would focus a different control. */
    uint32_t focused = 0;
    if (menu && fx_native_page_row_index(&menu->page) < menu->descriptor.count)
        focused = menu->rows[fx_native_page_row_index(&menu->page)].word[6];
    int picker=slot==FX_NATIVE_UI_PAGES-1u;
    const FxNativeDialogStyle *dialog=picker && u->editing<u->count &&
        (u->controls[u->editing].kind==FX_UI_DIALOG || u->controls[u->editing].kind==FX_UI_DOCUMENT)?u->dialog:0;
    FxNativeEntry entries[FX_NATIVE_ROWS];
    uint32_t count = 0, title = u->text_base;
    if (picker) {
        if (u->editing >= u->count)
            return 0;
        const FxUiControl *c = &u->controls[u->editing];
        if(picker_stage(u,c,u->control_revisions[u->editing],u->revision)!=FX_OK)return 0;
        (void)picker_labels(u,c);
        title = u->text_base + u->editing;
        if(dialog)title+=FX_UI_CONTROLS;
        /* Stable option objects exist before publication; live changes only
         * update their copied text and original native participation. */
        for (uint32_t i = 0; i < (dialog?2u:FX_UI_OPTIONS); ++i) {
            entries[count++] = (FxNativeEntry){u->text_base + 2u * FX_UI_CONTROLS + i,
                                               u->row_base + FX_UI_CONTROLS + i,
                                               0,
                                               0,
                                               u->options[i],
                                               c->kind==FX_UI_DOCUMENT?u->profile.stay:u->profile.back};
        }
    } else {
        if (slot)
            title = u->text_base + slot - 1u;
        else if(u->source)title=u->text_base;
        else if(u->root_control) {
            for(uint32_t i=0;i<u->count;++i)if(u->controls[i].id==u->root_control)title=u->text_base+i;
        } else {
            for(uint32_t i=0;i<u->count;++i)if(!(u->controls[i].flags&FX_UI_RETIRED) && !u->controls[i].parent) {
                title=u->text_base+i;break;
            }
        }
        /* Nikon's own pages retain fixed row identities and toggle native
         * visibility. Reserve every control slot before publishing this page,
         * so later module controls never replace a live widget/list/page. */
        for (uint32_t i = 0; i < FX_UI_CONTROLS; ++i) {
            if(u->source && !i)continue; /* Copied parent is a title, never a row. */
            const FxUiControl *c = i<u->count?&u->controls[i]:0;
            uint64_t next = u->profile.stay;
            uint32_t submenu = !c || u->source?1u:0u; /* Paged slots may later bind a different kind. */
            if (c && c->kind == FX_UI_PAGE) {
                next = u->source && u->source->destination?u->source->destination(u,i):u->page_base+i+1u;
                submenu = 1u;
            }
            if (c && (c->kind == FX_UI_CHOICE || c->kind == FX_UI_SWITCH || c->kind == FX_UI_DIALOG || c->kind == FX_UI_EDIT || c->kind == FX_UI_DOCUMENT)) {
                next = fx_native_ui_picker_id(u);
                submenu = 1u;
            }
            entries[count++] = (FxNativeEntry){
                u->text_base + FX_NATIVE_UI_ROW_LABELS + i, u->row_base + i,
                submenu,          1u, /* A status may become nonempty while this row is open. */
                u->actions[i],    next};
        }
    }
    if(!dialog && u->request_refresh)entries[count++] = (FxNativeEntry){
        u->text_base + FX_NATIVE_UI_NOTICE_TEXT,u->row_base+41u,0u,0u,u->actions[0],u->profile.stay};
    /* A native Back row also makes an empty/finished page navigable. */
    if(!dialog)entries[count++] = (FxNativeEntry){
        u->text_base + FX_NATIVE_UI_BACK_TEXT, u->row_base + 40u, 0, 0, u->callbacks[4], u->profile.back};
    if (!menu) {
        menu = fx_native_ui_allocate(u,(uint32_t)sizeof(*menu));
        if (!menu)
            return 0;
        u->pages[slot] = menu;
        fx_native_ui_keep(u,&u->page_accounted[slot],(uint32_t)sizeof(*menu));
    }
    memset(menu, 0, sizeof(*menu));
    FxNativeMenuProfile profile = u->profile;
    profile.page=parent;
    profile.back = profile.page->identity >> 16;
    /* These were prepared before resolving the logical parent. */
    for (uint32_t i = 0; i < count; ++i)
        if (entries[i].next == u->profile.back)
            entries[i].next = profile.back;
    if (fx_native_menu_build_style(menu, &profile, fx_native_ui_page_id(u,slot), title, entries, count, u->callbacks,dialog) != FX_OK)
        return 0;
    /* Selection can navigate only after its copied intent was accepted.
     * A notification callback's return value is not a navigation condition. */
    for(uint32_t row=0u;row<count;++row)if(entries[row].row!=u->row_base+40u)
        for(uint32_t e=0u;e<FX_NATIVE_EVENTS;++e) {
            uint32_t event=(uint32_t)menu->handlers[row][e].event&65535u;
            if(event==profile.terminal)break;
            for(uint32_t k=0u;k<4u;++k)if(event==profile.select_events[k]) {
                menu->handlers[row][e].condition=entries[row].action;
                menu->handlers[row][e].callback=u->callbacks[4];
            }
        }
    if(picker && u->controls[u->editing].kind==FX_UI_DOCUMENT &&
       fx_native_menu_reader_layout(menu,u->reader_profile->width,u->reader_profile->height)!=FX_OK)return 0;
    if (picker) {
        /* Initial data is published before original page-open/draw. Input is
         * still excluded by pinned until the native page actually opens. */
        picker_publish(u);
        uint64_t active=atomic_load(&u->picker_active);
        menu->page.selected = u->controls[u->editing].kind==FX_UI_DOCUMENT?1u:(uint16_t)u->picker_views[active&1u].control.value;
    }
    else if (focused)
        for (uint32_t i = 0; i < count; ++i)
            if (entries[i].row == focused) {
                menu->page.selected = (uint16_t)i;
                break;
            }
    if(!picker && !focused) {
        /* A short list otherwise leaves its hidden parent row in the native
         * viewport. Start at the first actual visible descriptor identity. */
        uint32_t first=count-1u;
        for(uint32_t i=0u;i<u->count;++i) {
            uint32_t identity=u->row_base+u->order[i];
            if(fx_native_ui_row_visible(u,menu->page.identity>>16,identity)) {
                for(uint32_t row=0u;row<count;++row)if(menu->rows[row].word[6]==identity){first=row;break;}
                break;
            }
        }
        menu->page.selected=0u;menu->page.tail=first;
    }
    if(fx_native_ui_order_page(u,&menu->page)!=FX_OK)return 0;
    return &menu->page;
}
FxNativePage *fx_native_ui_page(FxNativeUi *u,uint32_t page) {
    if(u && u->source && u->source->page)return u->source->page(u,page);
    return fx_native_ui_view_page(u,page);
}
FxNativePage *fx_native_ui_view_page(FxNativeUi *u,uint32_t page) {
    uint32_t slot=fx_native_ui_slot(u,page);
    if(!u || !u->owner || slot>=FX_NATIVE_UI_PAGES)return 0;
    if(slot==FX_NATIVE_UI_PAGES-1u && atomic_load_explicit(&u->owner->ui_cancel_state,memory_order_acquire))return 0;
    if(slot==FX_NATIVE_UI_PAGES-1u && fx_native_ui_editor_current(u))return &u->editor->native.page;
    FxNativeMenu *cached=u->pages[slot];
    if(cached && u->pinned[slot])return &cached->page;
    if(snapshot(u)!=FX_OK)return cached?&cached->page:0;
    cached=u->pages[slot];
    if(u->source && !slot && cached) {
        /* Nikon history retains this exact list object, even while closed.
         * Only copied rows/programs change; never reset its widget pointers. */
        return fx_native_ui_order_page(u,&cached->page)==FX_OK?&cached->page:0;
    }
    uint8_t chain[FX_NATIVE_UI_PAGES];uint32_t count=0u,seen=0u;
    const FxNativePage *parent=u->profile.page;
    for(;;) {
        if(seen&(1u<<slot))return 0;
        seen|=1u<<slot;
        if(u->pages[slot] && (u->pinned[slot] || (u->source && !slot))){parent=&u->pages[slot]->page;break;}
        int picker=slot==FX_NATIVE_UI_PAGES-1u;
        if(picker) {
            if(u->editing>=u->count)return 0;
        } else if(slot) {
            if(slot>u->count || u->controls[slot-1u].kind!=FX_UI_PAGE ||
               (u->controls[slot-1u].flags&FX_UI_ROOT))return 0;
        } else if(u->root_control) {
            for(uint32_t i=0;i<u->count;++i)
                if(u->controls[i].id==u->root_control && !(u->controls[i].flags&FX_UI_VISIBLE))return 0;
        }
        chain[count++]=(uint8_t)slot;
        if(!slot)break;
        uint32_t id=picker?u->controls[u->editing].parent:u->controls[slot-1u].parent;
        slot=0u;
        if(id) {
            uint32_t i=0u;while(i<u->count && u->controls[i].id!=id)++i;
            if(i==u->count)return 0;
            /* ROOT remains the native root even after its identity retires. */
            if(!(u->controls[i].flags&FX_UI_ROOT))slot=i+1u;
        }
    }
    while(count) {
        parent=build_page(u,chain[--count],parent);
        if(!parent)return 0;
    }
    return (FxNativePage *)parent;
}
FxResult fx_native_ui_retirement_target(FxNativeUi *u,uint32_t page,uint32_t *destination) {
    if(u && u->source && u->source->retirement)return u->source->retirement(u,page,destination);
    uint32_t index=fx_native_ui_slot(u,page);
    if(!u || !u->owner || !destination || index>=FX_NATIVE_UI_PAGES)return FX_ARGUMENT;
    uint32_t id=(uint32_t)u->focus_tokens[index];
    if(!id) {
        if(index==FX_NATIVE_UI_PAGES-1u)id=u->editing<u->count?u->controls[u->editing].id:0u;
        else if(index)id=index<=u->count?u->controls[index-1u].id:0u;
        else id=u->root_control;
    }
    uint32_t slot;FxResult r=fx_ui_retired_parent_slot(u->owner,id,&slot);
    if(r==FX_OK) {
        if(slot==UINT32_MAX)*destination=u->profile.page->identity>>16;
        else if(slot<FX_NATIVE_UI_PAGES-1u)*destination=u->page_base+slot;
        else r=FX_INTEGRITY;
    }
    return r;
}
void fx_native_ui_identity(FxNativeUi *u,const char *identity) {
    if(u && identity)utf16(u->contribution_title,identity);
}
int fx_native_ui_root_visible(FxNativeUi *u) {
    if(!u || !u->owner)return 0;
    uint32_t count=0u,revision=0u;
    FxResult r=fx_ui_snapshot(u->owner,u->pending,FX_UI_CONTROLS,&count,&revision);
    const FxUiControl *controls=r==FX_OK?u->pending:u->controls;
    if(r!=FX_OK)count=u->count;
    for(uint32_t i=0;i<count;++i)if((controls[i].flags&(FX_UI_ROOT|FX_UI_RETIRED))==FX_UI_ROOT)
        return (controls[i].flags&FX_UI_VISIBLE)!=0u;
    for(uint32_t i=0;i<count;++i)if((controls[i].flags&(FX_UI_RETIRED|FX_UI_VISIBLE))==FX_UI_VISIBLE)return 1;
    return count==0u; /* Compatibility with a module's legacy action entry. */
}
FxResult fx_native_ui_order_page(FxNativeUi *u,FxNativePage *page) {
    if(!u || !page || !u->owner)return FX_ARGUMENT;
    uint32_t id=page->identity>>16;
    uint32_t index=fx_native_ui_slot(u,id);
    if(index>=FX_NATIVE_UI_PAGES)return FX_ARGUMENT;
    if(index==FX_NATIVE_UI_PAGES-1u){u->layout_revisions[index]=u->revision;return FX_OK;}
    FxNativeMenu *m=u->pages[index];
    if(!m || page!=&m->page || m->descriptor.rows!=(uintptr_t)m->rows || m->descriptor.events!=(uintptr_t)m->events ||
       !m->descriptor.count || m->descriptor.count>FX_NATIVE_ROWS || u->count>FX_UI_CONTROLS)return FX_STATE;
    uint8_t desired[FX_NATIVE_ROWS];uint32_t count=0u,seen=0u;
    for(uint32_t i=0;i<u->count;++i) {
        uint32_t slot=u->order[i];
        if(slot>=u->count || (seen&(1u<<slot)))return FX_INTEGRITY;
        seen|=1u<<slot;
        if(u->source && !slot)continue;
        if(u->source || !(u->controls[slot].flags&FX_UI_RETIRED))desired[count++]=(uint8_t)slot;
    }
    for(uint32_t i=u->count;i<FX_UI_CONTROLS;++i)desired[count++]=(uint8_t)i;
    if(u->request_refresh)desired[count++]=41u;
    desired[count++]=40u; /* Retained native Back identity. */
    uint32_t focus=fx_native_page_row_index(page),focused=focus<m->descriptor.count?m->rows[focus].word[6]:0u;
    for(uint32_t i=0;i<count;++i) {
        uint32_t row=u->row_base+desired[i],j=0u;
        while(j<m->descriptor.count && m->rows[j].word[6]!=row)++j;
        if(j==m->descriptor.count)return FX_STATE; /* No partial mutation on invalid input. */
    }
    uint32_t changed=m->descriptor.count!=count;
    for(uint32_t i=0;i<count;++i) {
        uint32_t row=u->row_base+desired[i],j=i;
        while(j<m->descriptor.count && m->rows[j].word[6]!=row)++j;
        if(j!=i) {
            FxNativeRow saved=m->rows[i];m->rows[i]=m->rows[j];m->rows[j]=saved;
            uintptr_t events=m->events[i];m->events[i]=m->events[j];m->events[j]=events;
            FxNativeRowProgram *program=m->programs[i];m->programs[i]=m->programs[j];m->programs[j]=program;changed=1u;
        }
    }
    m->descriptor.count=(uint16_t)count;
    u->layout_revisions[index]=u->revision;
    if(changed) {
        uint32_t selected=count-1u,first=UINT32_MAX;
        for(uint32_t i=0;i<count;++i)if(fx_native_ui_row_visible(u,id,m->rows[i].word[6])) {
            if(first==UINT32_MAX)first=i;
            if(m->rows[i].word[6]==focused){first=i;break;}
        }
        if(first!=UINT32_MAX)selected=first;
        page->selected=0u;page->tail=(page->tail&~UINT64_C(0xffff))|selected;
    }
    if(u->recovery_focus && u->recovery_page==id) {
        for(uint32_t i=0u;i<count;++i)if(m->rows[i].word[6]==u->row_base+41u) {
            page->selected=0u;page->tail=(page->tail&~UINT64_C(0xffff))|i;break;
        }
    } else if(u->recovery_restore) {
        if(u->source)fx_native_ui_window_restore(u,u->recovery_return);
        else for(uint32_t i=0u;i<count;++i)if(m->rows[i].word[6]==(uint32_t)u->recovery_return &&
            fx_native_ui_row_visible(u,id,m->rows[i].word[6])) {
            page->selected=0u;page->tail=(page->tail&~UINT64_C(0xffff))|i;break;
        }
    }
    return FX_OK;
}
static void recovery_clear(FxNativeUi *u) {
    if(u->recovery_notice){u->recovery_notice=0u;u->recovery_dirty=1u;u->recovery_restore=1u;}
    u->recovery_focus=0u;u->recovery_error=FX_OK;
}
static void recovery_note(FxNativeUi *u,uint32_t page,uint32_t kind,FxResult error) {
    if(u->recovery_notice==FX_NATIVE_UI_RECOVER_NAVIGATION && kind==FX_NATIVE_UI_RECOVER_UPDATE)
        kind=FX_NATIVE_UI_RECOVER_NAVIGATION;
    if(u->recovery_notice!=kind || u->recovery_page!=page || u->recovery_error!=error) {
        if(!u->recovery_notice && kind==FX_NATIVE_UI_RECOVER_UPDATE) {
            FxNativeMenu *m=u->pages[fx_native_ui_slot(u,page)];uint32_t at=fx_native_page_row_index(&m->page);
            u->recovery_return=u->source?fx_native_ui_window_focus(u):at<m->descriptor.count?m->rows[at].word[6]:u->row_base+40u;
        }
        u->recovery_notice=kind;u->recovery_page=page;u->recovery_error=error;
        u->recovery_dirty=u->recovery_focus=1u;u->recovery_restore=0u;
    }
}
static FxResult navigation_active(FxNativeUi *u) {
    uint32_t slot=u->navigation_slot;
    if(slot>=u->count || u->controls[slot].id!=u->navigation_control ||
       u->controls[slot].kind!=u->navigation_kind || u->controls[slot].parent!=u->navigation_parent)return FX_STALE;
    return fx_ui_observed_active(u->owner,&u->controls[slot]);
}
FxResult fx_native_ui_prepare_navigation(FxNativeUi *u,const FxNativePage *page,uint32_t *destination) {
    if(!u || !page || !destination)return FX_ARGUMENT;
    *destination=0u;if(!u->request_refresh || u->navigation_pending!=1u)return FX_EMPTY;
    if((page->identity>>16)!=u->navigation_source || !fx_native_ui_page_matches(u,page) ||
       !u->pinned[fx_native_ui_slot(u,u->navigation_source)]) {
        u->navigation_pending=0u;recovery_clear(u);return FX_EMPTY;
    }
    FxResult active=navigation_active(u);if(active==FX_BUSY)return active;
    if(active!=FX_OK){u->navigation_pending=0u;recovery_clear(u);return FX_EMPTY;}
    FxNativePage *prepared=fx_native_ui_page(u,u->navigation_target);
    if(!prepared) {
        u->navigation_pending=2u;recovery_note(u,u->navigation_source,FX_NATIVE_UI_RECOVER_NAVIGATION,FX_BUDGET);return FX_EMPTY;
    }
    active=navigation_active(u);if(active==FX_BUSY)return active;
    if(active!=FX_OK){u->navigation_pending=0u;recovery_clear(u);return FX_EMPTY;}
    *destination=u->navigation_target;u->navigation_pending=0u;recovery_clear(u);return FX_OK;
}
FxResult fx_native_ui_action(FxNativeUi *u, uint32_t slot) {
    if (!u || slot >= u->count)
        return FX_ARGUMENT;
    if(u->owner->phase!=FX_OWNER_RUNNING)return FX_STATE;
    FxUiControl *c = &u->controls[slot];
    if((c->flags&(FX_UI_ENABLED|FX_UI_VISIBLE))!=(FX_UI_ENABLED|FX_UI_VISIBLE))return FX_EMPTY;
    if (c->kind == FX_UI_ACTION)
        return fx_ui_route_observed(u->owner,c,u->control_revisions[slot],FX_INPUT_ACTIVATE,0u);
    FxResult active=fx_ui_observed_active(u->owner,c);if(active!=FX_OK)return active;
    /* Selecting a row does not prove the target page opened. Its original
     * native open callback publishes focus, even when this queue is full. */
    if (c->kind != FX_UI_PAGE) {
        if(u->pinned[FX_NATIVE_UI_PAGES-1u] || atomic_load_explicit(&u->owner->ui_cancel_state,memory_order_acquire))return FX_BUSY;
        if(u->source) {
            /* A fresh forward session cannot reuse another control's native
             * widget class; the old session remains allocated for late refs. */
            atomic_store(&u->pages[FX_NATIVE_UI_PAGES-1u],0);
            atomic_store(&u->page_accounted[FX_NATIVE_UI_PAGES-1u],0u);
        }
        u->editing = slot;
    }
    return FX_OK;
}
FxResult fx_native_ui_option(FxNativeUi *u, uint32_t option) {
    if (!u || u->editing >= u->count)
        return FX_STATE;
    if(!u->pinned[FX_NATIVE_UI_PAGES-1u])return FX_STALE;
    if(fx_native_ui_reader_current(u))return fx_native_ui_reader_option(u,option);
    FxNativePickerView view;FxResult r=fx_native_ui_picker_snapshot(u,&view);if(r!=FX_OK)return r;
    const FxUiControl *c=&view.control;
    if (c->kind == FX_UI_DIALOG) {
        if (option > 1u)
            return FX_ARGUMENT;
        r = fx_ui_route_observed(u->owner,c,view.revision,option ? FX_INPUT_CANCEL : FX_INPUT_ACCEPT,option);
    } else r = fx_ui_route_observed(u->owner,c,view.revision,FX_INPUT_CHANGE,option);
    if (r == FX_OK)
        u->picker_completed = 1u;
    return r;
}
FxResult fx_native_ui_event(FxNativeUi *u,const FxNativePage *page,const FxNativeEvent *event) {
    if(!u || !u->owner || !page || !event)return FX_ARGUMENT;
    uint32_t id=page->identity>>16;
    uint32_t slot=fx_native_ui_slot(u,id);
    if(slot>=FX_NATIVE_UI_PAGES)return FX_STALE;
    FxNativeMenu *menu=u->pages[slot];
    if(!menu || !u->pinned[slot] || page->descriptor!=(uintptr_t)&menu->descriptor)return FX_STALE;
    if(menu->descriptor.events!=(uintptr_t)menu->events || menu->descriptor.rows!=(uintptr_t)menu->rows ||
       menu->descriptor.count>FX_NATIVE_ROWS)return FX_INTEGRITY;
    uintptr_t address=(uintptr_t)event,first=(uintptr_t)menu->handlers,last=first+sizeof(menu->handlers);
    for(uint32_t row=0;row<menu->descriptor.count;++row) {
        uintptr_t begin=menu->events[row];
        const FxNativeRowProgram *program=menu->programs[row];
        if(program) {
            uint32_t local=menu->rows[row].word[6]-u->row_base;
            if(!u->source || program->menu!=menu || program->row!=menu->rows[row].word[6] ||
               begin!=(uintptr_t)program->events || local>=FX_UI_CONTROLS ||
               program->control!=(local<u->count?u->controls[local].id:0u))return FX_INTEGRITY;
        } else if(begin<first || begin>last-FX_NATIVE_EVENTS*sizeof(*event) ||
           (begin-first)%(FX_NATIVE_EVENTS*sizeof(*event)))return FX_INTEGRITY;
        if(address<begin || address>=begin+FX_NATIVE_EVENTS*sizeof(*event))continue;
        if((address-begin)%sizeof(*event))return FX_ARGUMENT;
        uint32_t code=(uint32_t)event->event&65535u,selection=0u;
        for(uint32_t i=0;i<4u;++i)selection|=code==u->profile.select_events[i];
        if(!selection)return FX_EMPTY;
        uint32_t ident=menu->rows[row].word[6];
        if(!fx_native_ui_row_visible(u,id,ident))return FX_EMPTY;
        if(ident==u->row_base+41u) {
            if(!u->request_refresh || !u->recovery_notice || u->owner->phase!=FX_OWNER_RUNNING)return FX_STATE;
            if(u->recovery_notice==FX_NATIVE_UI_RECOVER_NAVIGATION)u->navigation_pending=1u;
            u->request_refresh(u->owner);return FX_BUSY;
        }
        if(slot==FX_NATIVE_UI_PAGES-1u) {
            if(ident<u->row_base+FX_UI_CONTROLS || ident-u->row_base-FX_UI_CONTROLS>=FX_UI_OPTIONS)return FX_STALE;
            return fx_native_ui_option(u,ident-u->row_base-FX_UI_CONTROLS);
        }
        if(ident<u->row_base || ident-u->row_base>=FX_UI_CONTROLS)return FX_STALE;
        uint32_t control=ident-u->row_base;
        FxResult result=fx_native_ui_action(u,control);
        if(result==FX_OK && u->request_refresh && u->controls[control].kind!=FX_UI_ACTION) {
            u->navigation_pending=1u;u->navigation_source=id;u->navigation_target=(uint32_t)event->next;
            u->navigation_slot=control;u->navigation_control=u->controls[control].id;
            u->navigation_kind=u->controls[control].kind;u->navigation_parent=u->controls[control].parent;
            u->recovery_return=u->source?fx_native_ui_window_focus(u):ident;
            u->request_refresh(u->owner);return FX_BUSY;
        }
        return result;
    }
    return FX_STALE;
}
int fx_native_ui_row_submenu(const FxNativeUi *u,uint32_t row) {
    if(!u || row<u->row_base || row-u->row_base>=u->count)return 0;
    return u->controls[row-u->row_base].kind!=FX_UI_ACTION;
}
int fx_native_ui_row_visible(const FxNativeUi *u,uint32_t page,uint32_t row) {
    uint32_t slot=fx_native_ui_slot(u,page);
    if(!u || slot>=FX_NATIVE_UI_PAGES)return 0;
    if(row==u->row_base+40u)return 1;
    if(row==u->row_base+41u)return u->request_refresh && u->recovery_notice && u->recovery_page==page;
    uint32_t index=row-u->row_base;
    if(slot==FX_NATIVE_UI_PAGES-1u)return u->editing<u->count && index>=FX_UI_CONTROLS &&
        index-FX_UI_CONTROLS<picker_count(&u->controls[u->editing]) &&
        (u->controls[u->editing].flags&FX_UI_VISIBLE)!=0;
    if(index>=u->count)return 0;
    uint32_t parent=slot && slot<=u->count?u->controls[slot-1u].id:u->root_control;
    const FxUiControl *c=&u->controls[index];
    return c->parent==parent && (c->flags&FX_UI_VISIBLE)!=0;
}
FxResult fx_native_ui_open(FxNativeUi *u, uint32_t page) {
    uint32_t i=fx_native_ui_slot(u,page);
    if(!u || !u->owner || i>=FX_NATIVE_UI_PAGES)return FX_ARGUMENT;
    uint32_t control=u->root_control;
    if(i==FX_NATIVE_UI_PAGES-1u) {
        if(atomic_load_explicit(&u->owner->ui_cancel_state,memory_order_acquire))return FX_BUSY;
        if(u->editing>=u->count)return FX_STATE;
        control=u->controls[u->editing].id;
    } else if(i) {
        if(i>u->count || u->controls[i-1u].kind!=FX_UI_PAGE ||
           (u->controls[i-1u].flags&FX_UI_ROOT))return FX_ARGUMENT;
        control=u->controls[i-1u].id;
    }
    FxResult r=FX_OK;uint32_t retained=0u;
    if(!u->pinned[i]) {
        r=fx_ui_page_reference(u->owner,1);if(r!=FX_OK)return r;retained=1u;
        u->pinned[i] = 1u;
        if (i == FX_NATIVE_UI_PAGES - 1u)
            u->picker_completed = 0;
    }
    uint64_t previous_focus=u->focus_tokens[i];
    r=fx_ui_focus_open(u->owner,control,&u->focus_tokens[i]);
    if(r==FX_OK && i==FX_NATIVE_UI_PAGES-1u && u->controls[u->editing].kind==FX_UI_DOCUMENT && u->reader)u->reader->opened=1u;
    if(r!=FX_OK && retained){u->pinned[i]=0u;(void)fx_ui_page_reference(u->owner,0);}
    if(r==FX_OK && (u->layout_revisions[i]!=u->revision || previous_focus!=u->focus_tokens[i])) {
        /* A pinned ancestor may have retained an older descriptor while a
         * child/picker was current. Its owning UI-loop commit must run even
         * when the worker has finished and sends no further publications. */
        if(u->layout_revisions[i]!=u->revision)u->structure_pending=1u;
        atomic_store_explicit(&u->presented_revision,0u,memory_order_release);
    }
    return r;
}
FxResult fx_native_ui_close(FxNativeUi *u, uint32_t page) {
    uint32_t i=fx_native_ui_slot(u,page);
    if(!u || !u->owner || i>=FX_NATIVE_UI_PAGES)return FX_ARGUMENT;
    if (!u->pinned[i])
        return FX_STALE;
    FxResult r = fx_ui_page_reference(u->owner, 0);
    if (r == FX_OK) {
        u->pinned[i] = 0;
        if(u->navigation_source==page || u->recovery_page==page){u->navigation_pending=0u;recovery_clear(u);}
        if(u->source && i==FX_NATIVE_UI_PAGES-1u && u->editing<u->count) {
            u->return_focus=u->controls[u->editing].id;u->projection_pending=1u;
        }
        if(i==FX_NATIVE_UI_PAGES-1u) {u->picker_staged=0u;u->picker_rows_pending=0u;fx_native_ui_reader_close(u);}
        if(u->focus_tokens[i]) {
            (void)fx_ui_focus_close(u->owner,u->focus_tokens[i]);u->focus_tokens[i]=0u;
        }
        /* Native Left/Back closes the picker without selecting an option.
         * Report cancellation exactly once; a successful CHANGE/ACCEPT or
         * explicit Cancel option already delivered its own event. */
        if (i == FX_NATIVE_UI_PAGES - 1u && !u->picker_completed && u->editing < u->count &&
            u->owner->phase == FX_OWNER_RUNNING) {
            FxNativePickerView view;r=fx_native_ui_picker_snapshot(u,&view);
            if(r==FX_OK)r=fx_ui_defer_cancel(u->owner,&view.control,view.revision,view.control.value);
            if(r==FX_STALE || r==FX_STATE)r=FX_OK; /* Retired/terminal owner has no recipient. */
        }
        if(u->source) {
            if(i==FX_NATIVE_UI_PAGES-1u)u->editing=UINT32_MAX;
            if(u->source->closed)u->source->closed(u,i);
            fx_native_ui_refresh_owner(u->owner);
        }
    }
    return r;
}
uint32_t fx_native_ui_value(FxNativeUi *u, uint32_t row, int *enabled) {
    if (!u || !enabled || row < u->row_base)return 0;
    if(row==u->row_base+41u){*enabled=u->owner->phase==FX_OWNER_RUNNING;return 0u;}
    uint32_t i = row - u->row_base;
    if(i>=FX_UI_CONTROLS && i<FX_UI_CONTROLS+FX_UI_OPTIONS && u->editing<u->count) {
        *enabled=(u->controls[u->editing].flags&(FX_UI_ENABLED|FX_UI_VISIBLE))==(FX_UI_ENABLED|FX_UI_VISIBLE) &&
            i-FX_UI_CONTROLS<picker_count(&u->controls[u->editing]) && u->owner->phase==FX_OWNER_RUNNING;
        if(fx_native_ui_reader_current(u))*enabled=*enabled && fx_native_ui_reader_enabled(u,i-FX_UI_CONTROLS);
        return 0; /* Picker labels have no separate value column. */
    }
    if(i>=u->count) {
        if(i<FX_UI_CONTROLS) {
            *enabled=0; /* Reserved rows have resolved, empty value resources. */
            return u->text_base+FX_NATIVE_UI_ROW_VALUES+i;
        }
        return 0; /* Back remains navigable after FINISH. */
    }
    *enabled = (u->controls[i].flags & FX_UI_ENABLED) != 0 && u->owner->phase == FX_OWNER_RUNNING;
    if((u->controls[i].kind==FX_UI_DIALOG || u->controls[i].kind==FX_UI_DOCUMENT) && u->dialog)return 0u;
    return u->text_base + FX_NATIVE_UI_ROW_VALUES + i;
}
FxResult fx_native_ui_refresh_values(FxNativeUi *u,uint32_t *changed_rows) {
    if(!u || !u->owner || !changed_rows)return FX_ARGUMENT;
    *changed_rows=0u;
    /* The editor owns a session snapshot; late PUT is applied after close.
     * Never run ordinary list reflow/title mutation on a keyboard descriptor. */
    if(fx_native_ui_editor_current(u))return FX_EMPTY;
    if(fx_native_ui_reader_current(u))return fx_native_ui_reader_refresh(u,changed_rows);
    uint32_t picker=u->pinned[FX_NATIVE_UI_PAGES-1u];
    uint32_t all_options=((1u<<FX_UI_OPTIONS)-1u)<<FX_UI_CONTROLS,option_mask=0u;
    uint32_t count,revision;
    FxResult r=copied_layout(u,u->pending,&count,&revision,u->pending_control_revisions,u->pending_order);
    if(r!=FX_OK)return r;
    uint32_t reproject=u->source && !picker && u->projection_pending;
    uint32_t unsubmitted=atomic_load_explicit(&u->presented_revision,memory_order_acquire)!=u->revision;
    if(!unsubmitted)u->structure_pending=u->title_pending=u->picker_rows_pending=0u;
    if(revision==u->revision && !reproject) {
        if(!unsubmitted)return FX_EMPTY;
        *changed_rows=(u->count?(1u<<u->count)-1u:0u)|(picker?all_options:0u)|
            (u->structure_pending?FX_NATIVE_UI_STRUCTURE:0u)|
            (u->title_pending?FX_NATIVE_UI_TITLE:0u);return FX_OK;
    }
    if(count<u->count && !u->source)return FX_STATE;
    uint32_t structural=reproject || u->structure_pending || count!=u->count || memcmp(u->order,u->pending_order,count);
    for(uint32_t i=0;i<count && i<u->count;++i) {
        const FxUiControl *old=&u->controls[i],*next=&u->pending[i];
        if(old->id!=next->id || old->parent!=next->parent || old->kind!=next->kind ||
           ((old->flags^next->flags)&FX_UI_ROOT)) {
            if(!u->source)return FX_STATE;
            structural=1u;
        }
        if((old->flags^next->flags)&(FX_UI_VISIBLE|FX_UI_RETIRED))structural=1u;
    }
    if(picker) {
        if(u->editing>=count)return FX_STATE;
        const FxUiControl *old=&u->controls[u->editing],*next=&u->pending[u->editing];
        r=picker_stage(u,next,u->pending_control_revisions[u->editing],revision);
        if(r!=FX_OK)return r; /* Neither logical native snapshot nor text changed. */
        if(picker_count(old)!=picker_count(next))structural=1u;
        if((old->flags^next->flags)&(FX_UI_ENABLED|FX_UI_VISIBLE))option_mask=all_options;
        if(u->owner->phase!=FX_OWNER_RUNNING)option_mask=all_options;
    }
    FxNativeRowProgram *programs[FX_UI_CONTROLS];
    r=fx_native_ui_routes_prepare(u,count,programs);if(r!=FX_OK)return r;
    uint64_t focus=!picker && structural?(reproject?u->return_focus:fx_native_ui_window_focus(u)):0u;
    fx_native_ui_routes_commit(u,programs);
    for(uint32_t i=count;i<u->count;++i) {
        memset(&u->controls[i],0,sizeof(u->controls[i]));u->control_revisions[i]=0u;
        memset(u->labels[i],0,sizeof(u->labels[i]));memset(u->values[i],0,sizeof(u->values[i]));
        memset(u->row_labels[i],0,sizeof(u->row_labels[i]));memset(u->row_values[i],0,sizeof(u->row_values[i]));
    }
    for(uint32_t i=0;i<count;++i) {
        const FxUiControl *next=&u->pending[i];
        u->control_revisions[i]=u->pending_control_revisions[i];
        if(!memcmp(&u->controls[i],next,sizeof(*next)))continue;
        if(i<u->count && memcmp(u->controls[i].label,next->label,sizeof(next->label)))u->title_pending=1u;
        if(picker && i==u->editing && next->kind==FX_UI_DIALOG &&
           memcmp(u->controls[i].text,next->text,sizeof(next->text)))u->title_pending=1u;
        u->controls[i]=*next;utf16(u->labels[i],next->label);
        const char *value=next->text;
        if(next->kind==FX_UI_DIALOG && !*value)value=next->label;
        if(next->kind==FX_UI_SWITCH)value=next->value?"On":"Off";
        if(next->kind==FX_UI_CHOICE)value=next->options[next->value];
        utf16(u->values[i],value);
        memcpy(u->row_labels[i],u->labels[i],sizeof(u->labels[i]));
        memcpy(u->row_values[i],u->values[i],sizeof(u->values[i]));
        *changed_rows|=1u<<i;
    }
    u->count=count;u->revision=revision;memcpy(u->order,u->pending_order,count);
    u->root_control=0u;
    for(uint32_t i=0;i<count;++i)if((u->controls[i].flags&(FX_UI_ROOT|FX_UI_RETIRED))==FX_UI_ROOT)u->root_control=u->controls[i].id;
    if(!picker && structural)fx_native_ui_window_restore(u,focus);
    if(reproject)u->projection_pending=2u;
    if(picker)option_mask|=picker_labels(u,&u->controls[u->editing]);
    u->picker_rows_pending|=option_mask;
    if(structural) {
        u->structure_pending=1u;
        /* Paged slots already committed immutable replacement programs.
         * Legacy slots keep their original one-identity-per-slot contract. */
        for(uint32_t p=0;!u->source && p<FX_NATIVE_UI_PAGES-1u;++p)if(u->pages[p]) {
            FxNativeMenu *m=u->pages[p];
            for(uint32_t i=0;i<count;++i) {
                uint32_t kind=u->controls[i].kind;
                uint64_t next=(u->controls[i].flags&FX_UI_RETIRED)?u->profile.stay:kind==FX_UI_PAGE?(u->source && u->source->destination?u->source->destination(u,i):u->page_base+i+1u):
                    kind==FX_UI_ACTION?u->profile.stay:fx_native_ui_picker_id(u);
                for(uint32_t row=0;row<m->descriptor.count;++row)if(m->rows[row].word[6]==u->row_base+i)
                    m->rows[row].word[4]=(m->rows[row].word[4]&~UINT32_C(0x40))|(kind==FX_UI_ACTION?0u:0x40u);
                for(uint32_t e=0;e<FX_NATIVE_EVENTS;++e) {
                    uint32_t event=(uint32_t)m->handlers[i][e].event&65535u;
                    for(uint32_t k=0;k<4u;++k)if(event==u->profile.select_events[k])m->handlers[i][e].next=next;
                }
            }
        }
    }
    if(unsubmitted)*changed_rows=u->count?(1u<<u->count)-1u:0u;
    *changed_rows|=u->picker_rows_pending|(structural?FX_NATIVE_UI_STRUCTURE:0u)|
        (u->title_pending?FX_NATIVE_UI_TITLE:0u);
    /* A successful no-op publication may advance the acknowledgement without
     * painting. An earlier failed submission must instead retry every row. */
    return FX_OK;
}
FxResult fx_native_ui_recover_values(FxNativeUi *u,uint32_t page,uint32_t *changed) {
    FxResult result=fx_native_ui_refresh_values(u,changed);
    if(!u || !u->request_refresh || !changed)return result;
    uint32_t slot=fx_native_ui_slot(u,page);
    if(slot>=FX_NATIVE_UI_PAGES-1u || !u->pinned[slot] || !u->pages[slot])return result;
    if(result==FX_BUDGET)recovery_note(u,page,FX_NATIVE_UI_RECOVER_UPDATE,result);
    else if(result==FX_OK || result==FX_EMPTY) {
        if(u->recovery_notice==FX_NATIVE_UI_RECOVER_UPDATE)recovery_clear(u);
    } else return result;
    if(u->recovery_dirty){*changed|=FX_NATIVE_UI_STRUCTURE|FX_NATIVE_UI_NOTICE;return FX_OK;}
    return result==FX_BUDGET?FX_EMPTY:result;
}
uint32_t fx_native_ui_repaint_rows(const FxNativeUi *u,const FxNativePage *page,uint32_t changes) {
    if(!u || !page || !page->descriptor)return 0u;
    const FxNativeDescriptor *d=(const FxNativeDescriptor *)page->descriptor;
    if(!d->rows || !d->count || d->count>FX_NATIVE_ROWS)return 0u;
    if(changes&FX_NATIVE_UI_STRUCTURE)return (1u<<d->count)-1u;
    const FxNativeRow *rows=(const FxNativeRow *)d->rows;uint32_t result=0u;
    for(uint32_t i=0;i<d->count;++i) {
        uint32_t index=rows[i].word[6]-u->row_base;
        if(index<FX_UI_CONTROLS+FX_UI_OPTIONS && (changes&(1u<<index)))result|=1u<<i;
    }
    return result;
}
int fx_native_ui_text(FxNativeUi *u, uint32_t id, const uint16_t **out, uint32_t *bytes) {
    if (!u || !out || !bytes || id < u->text_base || id >= u->text_base + FX_NATIVE_UI_TEXTS)
        return 0;
    if(fx_native_ui_reader_text(u,id,out,bytes))return 1;
    uint32_t n = id - u->text_base;
    const uint16_t *value = 0;
    if (n < FX_UI_CONTROLS) {
        /* A reserved native widget still resolves its label at construction.
         * Supply a completed blank, rather than an unavailable empty resource. */
        static const uint16_t blank[]=u" ";
        value = n<u->count?u->labels[n]:blank;
    } else if (n < 2u * FX_UI_CONTROLS)
        value = u->values[n - FX_UI_CONTROLS];
    else if (n < 2u * FX_UI_CONTROLS + FX_UI_OPTIONS)
        value = u->option_text[n - 2u * FX_UI_CONTROLS];
    else if(n<FX_NATIVE_UI_ROW_VALUES)
        value=u->row_labels[n-FX_NATIVE_UI_ROW_LABELS];
    else if(n<FX_NATIVE_UI_BACK_TEXT)
        value=u->row_values[n-FX_NATIVE_UI_ROW_VALUES];
    else if(n==FX_NATIVE_UI_BACK_TEXT) {
        static const uint16_t back[] = u"Back";
        value = back;
    } else {
        static const uint16_t retry[] = u"UI unavailable; Retry";
        value = retry;
    }
    uint32_t len = 0;
    /* Native shaping treats an empty resource as unfinished and can retain
     * stale value pixels. A resolved blank is still visually empty; the
     * public control snapshot and its logical text remain unchanged. */
    static const uint16_t empty[]=u" ";
    if(!value[0])value=empty;
    while (len < FX_UI_TEXT && value[len])
        ++len;
    *out = value;
    *bytes = len * 2u;
    return 1;
}
