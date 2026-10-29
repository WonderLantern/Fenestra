#include "fenestra_native_ui.h"
#include "fenestra_memory.h"

static uint64_t destination(FxNativeUi *u,const FxUiControl *c,uint32_t slot) {
    if(!c || (c->flags&FX_UI_RETIRED))return u->profile.stay;
    if(c->kind==FX_UI_PAGE)return u->source->destination(u,slot);
    return c->kind==FX_UI_ACTION?u->profile.stay:fx_native_ui_picker_id(u);
}
static uint32_t row_index(const FxNativeUi *u,const FxNativeMenu *menu,uint32_t slot) {
    for(uint32_t row=0u;row<menu->descriptor.count;++row)
        if(menu->rows[row].word[6]==u->row_base+slot)return row;
    return UINT32_MAX;
}
static int same(const FxUiControl *a,const FxUiControl *b) {
    return (!a && !b) || (a && b && a->id==b->id && a->parent==b->parent && a->kind==b->kind);
}
void fx_native_ui_routes_abort(FxNativeUi *u,FxNativeRowProgram *pending[FX_UI_CONTROLS]) {
    for(uint32_t slot=0u;slot<FX_UI_CONTROLS;++slot)if(pending[slot]) {
        u->source->release(u,pending[slot],(uint32_t)sizeof(*pending[slot]),0u);pending[slot]=0;
    }
}
FxResult fx_native_ui_routes_prepare(FxNativeUi *u,uint32_t count,FxNativeRowProgram *pending[FX_UI_CONTROLS]) {
    if(!u || !pending || count>FX_UI_CONTROLS)return FX_ARGUMENT;
    memset(pending,0,FX_UI_CONTROLS*sizeof(*pending));
    if(!u->source || !u->pages[0])return FX_OK;
    if(!u->source->allocate || !u->source->release || !u->source->destination)return FX_ARGUMENT;
    FxNativeMenu *menu=u->pages[0];
    if(!menu->descriptor.count || menu->descriptor.count>FX_NATIVE_ROWS)return FX_INTEGRITY;
    FxResult result=FX_OK;
    for(uint32_t slot=1u;slot<FX_UI_CONTROLS;++slot) {
        const FxUiControl *old=slot<u->count?&u->controls[slot]:0,*next=slot<count?&u->pending[slot]:0;
        uint32_t row=row_index(u,menu,slot);
        if(row==UINT32_MAX){result=FX_INTEGRITY;goto failed;}
        const FxNativeEvent *events=(const FxNativeEvent *)menu->events[row];
        uint64_t target=destination(u,next,slot);uint32_t replace=!same(old,next);
        for(uint32_t e=0u;e<FX_NATIVE_EVENTS;++e) {
            uint32_t code=(uint32_t)events[e].event&65535u;
            if(code==u->profile.terminal)break;
            for(uint32_t k=0u;k<4u;++k)if(code==u->profile.select_events[k])replace|=events[e].next!=target;
        }
        if(!replace)continue;
        FxNativeRowProgram *program=fx_native_ui_allocate(u,(uint32_t)sizeof(*program));
        if(!program){result=FX_BUDGET;goto failed;}
        pending[slot]=program;
        result=fx_native_menu_link(program->events,u->profile.navigation,u->profile.terminal,
            u->profile.select_events,u->actions[slot],target);
        if(result!=FX_OK)goto failed;
        program->menu=menu;program->row=u->row_base+slot;program->control=next?next->id:0u;
        for(uint32_t e=0u;e<FX_NATIVE_EVENTS;++e) {
            uint32_t code=(uint32_t)program->events[e].event&65535u;
            if(code==u->profile.terminal)break;
            for(uint32_t k=0u;k<4u;++k)if(code==u->profile.select_events[k]) {
                program->events[e].condition=u->actions[slot];program->events[e].callback=u->callbacks[4];
            }
        }
    }
    return FX_OK;
failed:
    fx_native_ui_routes_abort(u,pending);return result;
}
void fx_native_ui_routes_commit(FxNativeUi *u,FxNativeRowProgram *pending[FX_UI_CONTROLS]) {
    for(uint32_t slot=0u;slot<FX_UI_CONTROLS;++slot)if(pending[slot]) {
        FxNativeRowProgram *program=pending[slot];FxNativeMenu *menu=program->menu;
        uint32_t row=row_index(u,menu,slot);
        menu->programs[row]=program;menu->events[row]=(uintptr_t)program->events;
        fx_allocation_observe(u->observer,(uint32_t)sizeof(*program),FX_ALLOC_PUBLISHED);pending[slot]=0;
    }
}
uint64_t fx_native_ui_window_focus(const FxNativeUi *u) {
    if(!u->source || !u->pages[0])return 0u;
    const FxNativeMenu *menu=u->pages[0];uint32_t index=fx_native_page_row_index(&menu->page);
    if(index>=menu->descriptor.count)return 0u;
    uint32_t row=menu->rows[index].word[6],slot=row-u->row_base;
    if(row==u->row_base+40u)return UINT64_C(1)<<32;
    if(slot>=u->count)return 0u;
    const FxUiControl *c=&u->controls[slot];
    return c->id?c->id:c->kind==FX_UI_PAGE && !(c->flags&FX_UI_ROOT)?UINT64_C(2)<<32:0u;
}
void fx_native_ui_window_restore(FxNativeUi *u,uint64_t focus) {
    if(!u->source || !u->pages[0])return;
    FxNativeMenu *menu=u->pages[0];uint32_t target=UINT32_MAX,selected=UINT32_MAX;
    if(focus==UINT64_C(1)<<32)target=u->row_base+40u;
    else for(uint32_t i=0u;i<u->count;++i) {
        const FxUiControl *c=&u->controls[i];
        if((focus && focus<=UINT32_MAX && c->id==(uint32_t)focus) ||
           (focus==UINT64_C(2)<<32 && !c->id && c->kind==FX_UI_PAGE && !(c->flags&FX_UI_ROOT)))target=u->row_base+i;
    }
    if(target==UINT32_MAX || !fx_native_ui_row_visible(u,u->page_base,target)) {
        target=u->row_base+40u;
        for(uint32_t i=0u;i<u->count;++i) {
            uint32_t slot=u->order[i];
            if(slot<u->count && fx_native_ui_row_visible(u,u->page_base,u->row_base+slot)) {
                target=u->row_base+slot;break;
            }
        }
    }
    for(uint32_t row=0u;row<menu->descriptor.count;++row)
        if(menu->rows[row].word[6]==target){selected=row;break;}
    if(selected!=UINT32_MAX) {
        menu->page.selected=0u;menu->page.tail=(menu->page.tail&~UINT64_C(0xffff))|selected;
    }
}
