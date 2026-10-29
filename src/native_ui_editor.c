#include "fenestra_native_ui_editor.h"
#include "fenestra_memory.h"
#include "fenestra_native_ui_callbacks.h"
/* Read-only layout for external emulation diagnostics; no runtime consumer. */
const uint32_t fx_native_ui_editor_metadata[]={0x46584544u,2u,sizeof(FxNativeUi),
    offsetof(FxNativeUi,editor),offsetof(FxNativeUi,pinned),offsetof(FxNativeUi,revision),
    offsetof(FxNativeUi,presented_revision),offsetof(FxNativeUi,editing),offsetof(FxNativeUi,count),
    offsetof(FxNativeUi,structure_pending),offsetof(FxNativeUi,title_pending),
    offsetof(FxNativeUiEditor,opened),offsetof(FxNativeUiEditor,completed),
    offsetof(FxNativeUi,refresh_stage),offsetof(FxNativeUi,refresh_row),offsetof(FxNativeUi,refresh_result)};
/* Session storage belongs to the module native view on the owned heap.
 * Nikon's current keyboard page serializes its singleton model. */
static uint32_t noop(void *page){(void)page;return 1u;}
static FxNativeUiEditor *session(void *page) {
    FxNativeUiEditor *e=page;
    return e && e->ui && e->ui->editor==e && e->opened?e:0;
}
static uint32_t opened(void *page) {
    FxNativeUiEditor *e=page;
    if(!e || !e->ui || e->ui->editor!=e)return 0u;
    if(e->opened)return 1u;
    FxNativeUi *u=e->ui;
    if(fx_native_ui_open(u,((FxNativePage *)page)->identity>>16)!=FX_OK)return 0u;
    e->opened=1u;
    ((void (*)(const FxNativeEditorConfig *,void *))u->editor_profile->initialize)(&e->config,page);
    ((void (*)(void *,uint32_t))u->editor_profile->ready)(page,0u);
    return 1u;
}
/* Native event condition: consume/leave only when the complete result can be
 * queued. BUSY retains the keyboard and its input for another confirm press. */
static uint32_t accept(void) {
    FxNativeUi *u=fx_native_ui_editor_input_owner();
    FxNativeUiEditor *e=u?u->editor:0;if(!e || !e->opened || e->completed)return 0u;
    if(!((uint32_t (*)(char *))e->ui->editor_profile->read)(e->result))return 0u;
    uint32_t n=0;while(n<sizeof(e->result) && e->result[n])++n;
    if(n==sizeof(e->result))return 0u;
    FxResult r=fx_ui_route_text_observed(e->ui->owner,&e->observed.control,e->observed.revision,e->result,n+1u);
    if(r!=FX_OK)return 0u;
    e->completed=1u;return 1u;
}
static uint32_t cancel_session(FxNativeUiEditor *e) {if(!e || !e->opened || e->completed)return 0u;
    FxResult r=fx_ui_route_observed(e->ui->owner,&e->observed.control,e->observed.revision,FX_INPUT_CANCEL,0u);
    if(r!=FX_OK && r!=FX_STALE && r!=FX_STATE)return 0u;
    e->completed=1u;return 1u;
}
static uint32_t cancel(void) {
    FxNativeUi *u=fx_native_ui_editor_input_owner();
    return cancel_session(u?u->editor:0);
}
static uint32_t closed(void *page) {
    FxNativeUiEditor *e=session(page);if(!e)return 1u;
    if(e->opened) {
        if(!e->completed) {
            /* Forced native close cannot veto navigation or wait for the
             * worker. Preserve the editor's original observed revision. */
            FxResult r=fx_ui_defer_cancel(e->ui->owner,&e->observed.control,e->observed.revision,0u);
            if(r==FX_OK || r==FX_STATE)e->completed=1u;
        }
        e->ui->picker_completed=1u; /* Cancellation used the original session. */
        (void)fx_native_ui_close(e->ui,((FxNativePage *)page)->identity>>16);
    }
    e->opened=0u;
    /* Native back navigation can reuse its parent without reopening it.
     * Deferred PUTs must be presented after the keyboard releases the view. */
    atomic_store_explicit(&e->ui->presented_revision,0u,memory_order_release);
    e->ui->structure_pending=1u;
    /* Core-owned views do not have a module worker polling their queue.
     * Drain only after unpinning: the recipient may replace the projection
     * immediately in response to accepted text or cancellation. */
    if(e->ui->input_ready)e->ui->input_ready(e->ui);
    fx_native_ui_refresh_owner(e->ui->owner);
    return 1u;
}
int fx_native_ui_editor_current(const FxNativeUi *u) {
    return u && u->editor && u->editor->opened;
}
FxNativePage *fx_native_ui_editor_page(FxNativeUi *u,const FxNativePage *parent) {
    if(!u || !parent || !u->editor_profile || u->editing>=u->count)return 0;
    const FxUiControl *control=&u->controls[u->editing];
    if(control->kind!=FX_UI_EDIT)return 0;
    if(fx_native_ui_editor_current(u))return &u->editor->native.page;
    const FxNativeUiEditorProfile *p=u->editor_profile;
    if(!p->style || !p->style->previous || !p->events || !p->count || p->count>FX_NATIVE_EDITOR_EVENTS-4u ||
       !p->initialize || !p->read || !p->ready || !p->transition || !p->current_page)return 0;
    const FxNativePage *current=((const FxNativePage *(*)(void))p->current_page)();
    if(current && current->descriptor &&
       ((const FxNativeDescriptor *)current->descriptor)->flags==UINT32_C(0x8000c010))return 0;
    if(!u->editor) {
        u->editor=fx_native_ui_allocate(u,(uint32_t)sizeof(*u->editor));if(!u->editor)return 0;
        fx_native_ui_keep(u,&u->editor_accounted,(uint32_t)sizeof(*u->editor));
    }
    FxNativeUiEditor *e=u->editor;memset(e,0,sizeof(*e));e->ui=u;
    e->observed=(FxNativePickerView){*control,u->control_revisions[u->editing]};
    memcpy(e->initial,control->text,sizeof(control->text));
    uint32_t maximum=control->value?control->value:36u;
    e->config=(FxNativeEditorConfig){.initial=(uintptr_t)e->initial,.keyboard=3u,.layout=10u,
        .rows=(int16_t)(maximum==54u?3u:2u),.maximum=(int16_t)maximum};
    FxNativeEvent events[FX_NATIVE_EDITOR_EVENTS];
    memcpy(events,p->events,p->count*sizeof(*events));
    for(uint32_t i=0;i<p->count;++i)events[i].next=u->profile.stay;
    /* Return to the actual invoking list for both core and module views.
     * The view's outside Back target may be a page ID or a history command;
     * neither identifies the list which opened this keyboard. */
    uint64_t back=parent->identity>>16;
    events[p->count]=(FxNativeEvent){p->accept,(uintptr_t)noop,(uintptr_t)accept,back};
    events[p->count+1u]=(FxNativeEvent){p->cancel,(uintptr_t)noop,(uintptr_t)cancel,back};
    events[p->count+2u]=(FxNativeEvent){p->back,(uintptr_t)noop,(uintptr_t)cancel,back};
    events[p->count+3u]=(FxNativeEvent){u->profile.terminal,0u,0u,u->profile.stay};
    const FxNativeEditorDescriptor *style=(const void *)p->style->descriptor;
    const FxNativeRow *row=(const void *)style->base.rows;
    /* The original keyboard shares the root surface, not its logical caller.
     * Return navigation is explicit in the event records above. */
    FxNativeEditorSpec spec={.style=p->style,.parent=(const FxNativePage *)p->style->previous,.events=events,
        .callbacks={(uintptr_t)opened,(uintptr_t)closed,p->transition,(uintptr_t)noop,(uintptr_t)noop},
        .page=fx_native_ui_picker_id(u),.title=u->text_base+u->editing,
        .row=u->row_base+u->editing,.help=row->word[1],.footer=row->word[5],
        .event_count=p->count+4u,.terminal=u->profile.terminal};
    if(fx_native_editor_build(&e->native,&spec)!=FX_OK)return 0;
    return &e->native.page;
}
