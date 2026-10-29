#include "fenestra_native_editor.h"
#include "fenestra_memory.h"

FxResult fx_native_editor_build(FxNativeEditor *out,const FxNativeEditorSpec *s) {
    if(!out || !s || !s->style || !s->parent || !s->events || s->reserved ||
       !s->page || s->page>65535u || !s->row || s->row>65535u ||
       !s->terminal || s->terminal>65535u || !s->event_count ||
       s->event_count>FX_NATIVE_EDITOR_EVENTS)return FX_ARGUMENT;
    if(out->page.identity)return FX_BUSY;
    if((s->style->identity&65535u)!=2u || !s->style->descriptor)return FX_ARGUMENT;
    const FxNativeEditorDescriptor *source=(const void *)s->style->descriptor;
    if(source->base.flags!=UINT32_C(0x8000c010) || source->base.reserved ||
       source->base.count!=1u || !source->base.header || !source->base.rows ||
       !source->hit_records || source->hit_record_count!=1u)return FX_INTEGRITY;
    for(uint32_t i=0;i<5u;++i)if(!s->callbacks[i])return FX_ARGUMENT;
    for(uint32_t i=0;i<s->event_count;++i) {
        uint32_t id=(uint32_t)s->events[i].event&65535u;
        if((id==s->terminal)!=(i==s->event_count-1u))return FX_INTEGRITY;
        for(uint32_t j=0;j<i;++j)
            if(id==((uint32_t)s->events[j].event&65535u))return FX_INTEGRITY;
    }
    /* No allocation, firmware call or IO; reject invalid descriptions before
     * touching output. Native runtime handles always begin empty. */
    memset(out,0,sizeof(*out));
    out->page.identity=(s->page<<16)|2u;
    out->page.previous=(uintptr_t)s->parent;
    out->page.callbacks=(uintptr_t)out->callbacks;
    out->page.descriptor=(uintptr_t)&out->descriptor;
    memcpy(out->callbacks,s->callbacks,sizeof(out->callbacks));
    out->descriptor=*source;
    memcpy(out->header,(const void *)source->base.header,sizeof(out->header));
    out->row=*(const FxNativeRow *)source->base.rows;
    out->row.word[1]=s->help;
    out->row.word[2]=s->title;
    out->row.word[5]=s->footer;
    out->row.word[6]=s->row;
    out->descriptor.base.header=(uintptr_t)out->header;
    out->descriptor.base.rows=(uintptr_t)&out->row;
    out->descriptor.base.events=(uintptr_t)&out->event_table;
    out->event_table=(uintptr_t)out->handlers;
    memcpy(out->handlers,s->events,s->event_count*sizeof(*s->events));
    return FX_OK;
}
