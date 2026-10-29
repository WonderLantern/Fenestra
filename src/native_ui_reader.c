#include "fenestra_native_ui_reader.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_memory.h"
const uint32_t fx_native_ui_reader_metadata[]={0x46585244u,1u,offsetof(FxNativeUi,reader),
    offsetof(FxNativeUiReader,opened),offsetof(FxNativeUiReader,pending),offsetof(FxNativeUiReader,offset),
    offsetof(FxNativeUiReader,next),offsetof(FxNativeUiReader,ready),offsetof(FxNativeUiReader,result),
    offsetof(FxNativeUiReader,version),offsetof(FxNativeUiReader,bytes),offsetof(FxNativeUiReader,display)};
static void *allocate(FxNativeUi *u,uint32_t bytes) {
    if(u->source && u->source->allocate)return fx_native_ui_allocate(u,bytes);
    const FxNamedBinding *b=fx_platform_find(u->owner,"rtos.memory.allocate"),*f=fx_platform_find(u->owner,"rtos.memory.release");
    if(!b || !f || b->kind!=FX_BIND_FUNCTION || f->kind!=FX_BIND_FUNCTION ||
       b->abi!=FX_CALL_U64_8 || f->abi!=FX_CALL_U64_8 || !u->owner->platform->io.call)return 0;
    uint64_t args[8]={bytes,0},result=0;
    if(u->owner->platform->io.call(u->owner->platform->io.context,b,args,&result)!=FX_OK)return 0;
    if(result)fx_allocation_observe(u->observer,bytes,FX_ALLOC_REQUESTED);
    return (void *)(uintptr_t)result;
}
static void release(FxNativeUi *u,void *memory,uint32_t bytes) {
    if(u->source && u->source->release){u->source->release(u,memory,bytes,0u);return;}
    const FxNamedBinding *b=fx_platform_find(u->owner,"rtos.memory.release");
    uint64_t args[8]={(uintptr_t)memory},ignored=0;
    if(memory && b && u->owner->platform->io.call(u->owner->platform->io.context,b,args,&ignored)==FX_OK)
        fx_allocation_observe(u->observer,bytes,FX_ALLOC_FREE_PENDING);
    else if(memory)fx_allocation_observe(u->observer,bytes,FX_ALLOC_PUBLISHED);
}
static void message(FxNativeUiReader *r,const char *text) {
    uint32_t n=0;while(text[n] && n<FX_TEXT_PAGE_UNITS-1u){r->display[n]=(uint8_t)text[n];++n;}
    r->display[n]=0;
}
int fx_native_ui_reader_current(const FxNativeUi *u){return u && u->reader && u->reader->opened;}
void fx_native_ui_reader_close(FxNativeUi *u) {
    if(!u || !u->reader)return;
    FxNativeUiReader *r=u->reader;uint32_t opened=r->opened;
    while(r->history){FxReaderHistory *old=r->history;r->history=old->previous;release(u,old,(uint32_t)sizeof(*old));}
    fx_ui_document_release(r->document);r->document=0;r->opened=r->pending=r->ready=r->measuring=r->retry_pending=0;
    if(opened){atomic_store_explicit(&u->presented_revision,0u,memory_order_release);fx_native_ui_refresh_owner(u->owner);}
}
FxResult fx_native_ui_reader_begin(FxNativeUi *u) {
    if(!u || !u->owner || !u->reader_profile || !u->reader_profile->measure || !u->reader_profile->bounds || !u->dialog || u->editing>=u->count)return FX_ARGUMENT;
    if(fx_native_ui_reader_current(u))return FX_BUSY;
    fx_native_ui_reader_close(u);
    if(!u->reader){u->reader=allocate(u,(uint32_t)sizeof(*u->reader));if(!u->reader)return FX_BUDGET;
        fx_native_ui_keep(u,&u->reader_accounted,(uint32_t)sizeof(*u->reader));}
    FxNativeUiReader *r=u->reader;memset(r,0,sizeof(*r));
    if(u->text_regions){FxResult tracked=fx_native_text_regions_add(u->text_regions,&u->reader_text_region,r,sizeof(*r));if(tracked!=FX_OK)return tracked;}
    r->result=fx_ui_document_acquire(u->owner,u->controls[u->editing].id,&r->document);
    if(r->result!=FX_OK && r->result!=FX_EMPTY)return r->result;
    r->version=fx_ui_document_version(r->document);r->bytes=fx_ui_document_bytes(r->document);
    message(r,r->document?"Loading...":"Document not published");r->pending=1u;
    return FX_OK;
}
int fx_native_ui_reader_enabled(const FxNativeUi *u,uint32_t option) {
    if(!fx_native_ui_reader_current(u) || u->owner->phase!=FX_OWNER_RUNNING || !u->reader->ready || u->reader->pending)return 0;
    if(u->reader->result==FX_BUDGET)return option==1u && u->reader->retry_pending;
    if(u->reader->result!=FX_OK)return 0;
    return option==0u?u->reader->history!=0:option==1u && u->reader->next<fx_ui_document_bytes(u->reader->document);
}
FxResult fx_native_ui_reader_option(FxNativeUi *u,uint32_t option) {
    if(option>1u)return FX_ARGUMENT;
    if(!fx_native_ui_reader_enabled(u,option))return FX_EMPTY;
    u->reader->pending=u->reader->retry_pending?u->reader->retry_pending:option?2u:3u;
    atomic_store_explicit(&u->presented_revision,0u,memory_order_release);fx_native_ui_refresh_owner(u->owner);return FX_OK;
}
typedef struct {FxNativeUi *ui;void *object;} Measure;
static FxResult measure(void *context,const uint16_t *text,uint32_t units,int32_t dimensions[2]) {
    Measure *m=context;const FxNativeUiReaderProfile *p=m->ui->reader_profile;
    (void)text;(void)units;
    (void)((uint32_t (*)(void *))p->title.release_text)(m->object);
    int16_t size[2]={0,0};
    if(!((uint32_t (*)(void *,int16_t *))p->measure)(m->object,size))return FX_BUSY;
    dimensions[0]=size[0];dimensions[1]=size[1];return FX_OK;
}
FxResult fx_native_ui_reader_refresh(FxNativeUi *u,uint32_t *changed) {
    if(!u || !changed || !fx_native_ui_reader_current(u))return FX_ARGUMENT;
    FxNativeUiReader *r=u->reader;*changed=0;
    if(!r->pending) {
        if(atomic_load_explicit(&u->presented_revision,memory_order_acquire)==u->revision)return FX_EMPTY;
        *changed=FX_NATIVE_UI_TITLE|(3u<<FX_UI_CONTROLS);return FX_OK;
    }
    const FxNativeUiReaderProfile *p=u->reader_profile;
    FxNativePage *page=&u->pages[FX_NATIVE_UI_PAGES-1u]->page;void *object=0;
    FxResult result=fx_native_title_object(&p->title,page,&object);if(result!=FX_OK)return result;
    if(r->document) {
        int16_t bounds[4];
        /* Original modal body +0xd0 is the text rectangle. +0x10 includes
         * the icon and buttons; using it would paginate over those controls. */
        if(fx_native_page_modal(page))memcpy(bounds,(const uint8_t *)object+0xd0u,sizeof(bounds));
        else ((void (*)(void *,int16_t *))p->bounds)(object,bounds);
        uint32_t offset=r->pending==2u?r->next:r->pending==3u?r->history->offset:r->offset;
        Measure m={u,object};r->measuring=1u;
        result=fx_text_page(&r->work,fx_ui_document_text(r->document),fx_ui_document_bytes(r->document),offset,
            (int32_t)bounds[2]-bounds[0],(int32_t)bounds[3]-bounds[1],measure,&m);
        r->measuring=0u;(void)((uint32_t (*)(void *))p->title.release_text)(object);
        if(result==FX_OK) {
            if(r->pending==2u) {
                FxReaderHistory *entry=allocate(u,(uint32_t)sizeof(*entry));
                if(!entry) {
                    r->retry_pending=r->pending;r->pending=0u;r->ready=1u;r->result=FX_BUDGET;
                    message(r,"UI unavailable; select Retry");
                    static const uint16_t retry[]=u"Retry";memset(u->option_text[1],0,sizeof(u->option_text[1]));
                    memcpy(u->option_text[1],retry,sizeof(retry));
                    *changed=FX_NATIVE_UI_TITLE|(3u<<FX_UI_CONTROLS);return FX_OK;
                }
                *entry=(FxReaderHistory){r->history,r->offset};r->history=entry;
            } else if(r->pending==3u){FxReaderHistory *entry=r->history;r->history=entry->previous;release(u,entry,(uint32_t)sizeof(*entry));}
            memcpy(r->display,r->work.text,(r->work.units+1u)*sizeof(uint16_t));r->offset=offset;r->next=r->work.next;
            r->retry_pending=0u;static const uint16_t next[]=u"Next";
            memset(u->option_text[1],0,sizeof(u->option_text[1]));memcpy(u->option_text[1],next,sizeof(next));
        } else if(result==FX_BUSY)return result;
        else message(r,"Cannot lay out document");
        r->result=result;
    }
    r->pending=0;r->ready=1u;
    *changed=FX_NATIVE_UI_TITLE|(3u<<FX_UI_CONTROLS);return FX_OK;
}
int fx_native_ui_reader_text(FxNativeUi *u,uint32_t id,const uint16_t **text,uint32_t *bytes) {
    if(!u || !u->reader || u->editing>=u->count || u->controls[u->editing].kind!=FX_UI_DOCUMENT ||
       id!=u->text_base+FX_UI_CONTROLS+u->editing)return 0;
    const uint16_t *value=u->reader->measuring?u->reader->work.text:u->reader->display;
    static const uint16_t blank[]=u" ";if(!value[0])value=blank;
    uint32_t n=0;while(n<FX_TEXT_PAGE_UNITS && value[n])++n;
    *text=value;*bytes=n*2u;return 1;
}
