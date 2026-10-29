#include "fenestra_ui_document.h"
#include "fenestra_memory.h"
typedef struct {uint32_t control,version,received;uint64_t token,publisher;FxUiDocument *current,*staging;} DocumentSlot;
typedef struct {FxPlatformOwner *owner;_Atomic uint32_t lock;_Atomic uint32_t revoked;uint64_t sequence;DocumentSlot slots[FX_UI_CONTROLS];FxAllocationObserver observer;} Documents;
struct FxUiDocument {FxPlatformOwner *owner;_Atomic uint32_t references;uint32_t bytes,version;FxAllocationObserver observer;uint32_t retained;char text[];};
static int release_memory(FxPlatformOwner *o,void *p) {
    const FxNamedBinding *b=fx_platform_find(o,"rtos.memory.release");uint64_t ignored=0,args[8]={(uintptr_t)p};
    return p && b && o->platform->io.call && o->platform->io.call(o->platform->io.context,b,args,&ignored)==FX_OK;
}
static void *allocate(FxPlatformOwner *o,uint32_t bytes,FxAllocationObserver observer) {
    const FxNamedBinding *b=fx_platform_find(o,"rtos.memory.allocate");
    const FxNamedBinding *f=fx_platform_find(o,"rtos.memory.release");uint64_t address=0,args[8]={bytes,0};
    if(!b || !f || b->kind!=FX_BIND_FUNCTION || f->kind!=FX_BIND_FUNCTION ||
       b->abi!=FX_CALL_U64_8 || f->abi!=FX_CALL_U64_8 || !o->platform->io.call)return 0;
    if(o->platform->io.call(o->platform->io.context,b,args,&address)!=FX_OK || !address)return 0;
    void *p=(void *)(uintptr_t)address;memset(p,0,bytes);fx_allocation_observe(observer,bytes,FX_ALLOC_REQUESTED);return p;
}
static Documents *state(FxPlatformOwner *o,int create) {
    Documents *s=atomic_load_explicit(&o->ui_documents,memory_order_acquire);
    if(s || !create)return s;
    FxAllocationObserver observer=fx_platform_account_observer(o,"ui.documents");
    s=allocate(o,(uint32_t)sizeof(*s),observer);if(!s)return 0;s->owner=o;s->observer=observer;
    void *empty=0;
    if(!atomic_compare_exchange_strong_explicit(&o->ui_documents,&empty,s,memory_order_release,memory_order_acquire)) {
        if(release_memory(o,s))fx_allocation_observe(observer,(uint32_t)sizeof(*s),FX_ALLOC_FREE_PENDING);
        else fx_allocation_observe(observer,(uint32_t)sizeof(*s),FX_ALLOC_PUBLISHED);
        s=empty;
    } else fx_allocation_observe(observer,(uint32_t)sizeof(*s),FX_ALLOC_PUBLISHED);
    return s;
}
static int lock(Documents *s){uint32_t zero=0;return atomic_compare_exchange_strong_explicit(&s->lock,&zero,1u,memory_order_acquire,memory_order_relaxed);}
static void unlock(Documents *s){atomic_store_explicit(&s->lock,0u,memory_order_release);}
static DocumentSlot *slot(Documents *s,uint32_t id,int create) {
    DocumentSlot *empty=0;
    for(uint32_t i=0;i<FX_UI_CONTROLS;++i){if(s->slots[i].control==id)return &s->slots[i];if(!s->slots[i].control && !empty)empty=&s->slots[i];}
    if(empty && create)empty->control=id;
    return create?empty:0;
}
void fx_ui_document_release(FxUiDocument *d) {
    if(d && atomic_fetch_sub_explicit(&d->references,1u,memory_order_acq_rel)==1u) {
        FxAllocationObserver observer=d->observer;uint32_t bytes=(uint32_t)sizeof(*d)+d->bytes+1u,retained=d->retained;
        if(release_memory(d->owner,d))fx_allocation_observe(observer,bytes,retained?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);
        else if(!retained)fx_allocation_observe(observer,bytes,FX_ALLOC_PUBLISHED);
    }
}
static void retain(FxUiDocument *d) {
    if(d && !d->retained){d->retained=1u;fx_allocation_observe(d->observer,(uint32_t)sizeof(*d)+d->bytes+1u,FX_ALLOC_PUBLISHED);}
}
const char *fx_ui_document_text(const FxUiDocument *d){return d?d->text:0;}
uint32_t fx_ui_document_bytes(const FxUiDocument *d){return d?d->bytes:0u;}
uint32_t fx_ui_document_version(const FxUiDocument *d){return d?d->version:0u;}
FxResult fx_ui_document_acquire(FxPlatformOwner *o,uint32_t id,FxUiDocument **out) {
    if(!o || !o->platform || !id || !out)return FX_ARGUMENT;
    *out=0;
    Documents *s=state(o,0);if(!s)return FX_EMPTY;if(!lock(s))return FX_BUSY;
    DocumentSlot *p=slot(s,id,0);FxUiDocument *d=p?p->current:0;FxResult r=FX_EMPTY;
    if(d) {
        uint32_t references=atomic_load_explicit(&d->references,memory_order_relaxed);
        for(;;) {
            if(references==UINT32_MAX){r=FX_BUDGET;break;}
            if(atomic_compare_exchange_weak_explicit(&d->references,&references,references+1u,memory_order_relaxed,memory_order_relaxed)){*out=d;r=FX_OK;break;}
        }
    }
    unlock(s);return r;
}
static int utf8(const char *text,uint32_t bytes) {
    uint32_t i=0;
    while(i<bytes) {
        uint32_t c=(uint8_t)text[i++],extra=0,minimum=0;
        if(!c)return 0;
        if(c<128u)continue;
        if(c>=0xc2u && c<=0xdfu){extra=1u;minimum=0x80u;c&=31u;}
        else if(c>=0xe0u && c<=0xefu){extra=2u;minimum=0x800u;c&=15u;}
        else if(c>=0xf0u && c<=0xf4u){extra=3u;minimum=0x10000u;c&=7u;}
        else return 0;
        if(extra>bytes-i)return 0;
        while(extra--){uint32_t next=(uint8_t)text[i++];if((next&0xc0u)!=0x80u)return 0;c=(c<<6)|(next&63u);}
        if(c<minimum || c>0x10ffffu || (c>=0xd800u && c<=0xdfffu))return 0;
    }
    return 1;
}
static FxResult call(Documents *s,uint64_t publisher,const FxServiceRequest *q,FxServiceReply *a) {
    uint64_t op=q->arg[2];if(q->arg[3])return FX_ARGUMENT;
    a->bytes=0;
    if(op==FX_DOCUMENT_QUERY) {
        if(q->bytes)return FX_ARGUMENT;
        FxUiDocumentInfo info={1u,sizeof(info),FX_UI_DOCUMENT_BYTES,FX_UI_CONTROLS,FX_SERVICE_BYTES-sizeof(FxUiDocumentRequest),0u};
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(q->bytes<sizeof(FxUiDocumentRequest))return FX_ARGUMENT;
    FxUiDocumentRequest r;memcpy(&r,q->data,sizeof(r));
    if(!r.control || r.reserved || op>FX_DOCUMENT_READ)return FX_ARGUMENT;
    if(op==FX_DOCUMENT_WRITE) {if(r.bytes>FX_SERVICE_BYTES-sizeof(r) || q->bytes!=sizeof(r)+r.bytes)return FX_ARGUMENT;}
    else if(q->bytes!=sizeof(r))return FX_ARGUMENT;
    if((op==FX_DOCUMENT_BEGIN && (r.token || r.offset || r.bytes>FX_UI_DOCUMENT_BYTES)) ||
       ((op==FX_DOCUMENT_COMMIT || op==FX_DOCUMENT_ABORT || op==FX_DOCUMENT_CLEAR || op==FX_DOCUMENT_INFO) && (r.offset || r.bytes)) ||
       ((op==FX_DOCUMENT_CLEAR || op==FX_DOCUMENT_INFO) && r.token))return FX_ARGUMENT;
    /* Copy outside the store lock from a pinned immutable version. A UI
     * reader can use the same pin API while a publisher replaces this body. */
    if(op==FX_DOCUMENT_INFO || op==FX_DOCUMENT_READ) {
        if(atomic_load_explicit(&s->revoked,memory_order_acquire))return FX_STATE;
        FxUiDocument *d=0;FxResult result=fx_ui_document_acquire(s->owner,r.control,&d);
        if(result!=FX_OK)return result;
        uint32_t version=fx_ui_document_version(d),bytes=fx_ui_document_bytes(d);
        if(op==FX_DOCUMENT_INFO){a->value[0]=version;a->value[1]=bytes;}
        else if(!r.token || r.token!=version)result=FX_STALE;
        else if(r.offset>bytes || r.bytes>bytes-r.offset || r.bytes>FX_SERVICE_BYTES)result=FX_BOUNDS;
        else {memcpy(a->data,fx_ui_document_text(d)+r.offset,r.bytes);a->bytes=r.bytes;}
        fx_ui_document_release(d);return result;
    }
    FxUiDocument *fresh=0,*retired=0;
    if(op==FX_DOCUMENT_BEGIN) {
        fresh=allocate(s->owner,(uint32_t)sizeof(*fresh)+r.bytes+1u,s->observer);if(!fresh)return FX_BUDGET;
        fresh->owner=s->owner;fresh->observer=s->observer;fresh->bytes=r.bytes;atomic_init(&fresh->references,1u);
    }
    if(!lock(s)){fx_ui_document_release(fresh);return FX_BUSY;}
    FxResult result=FX_OK;DocumentSlot *p=s->revoked?0:slot(s,r.control,op==FX_DOCUMENT_BEGIN);
    if(s->revoked)result=FX_STATE;
    else if(!p)result=op==FX_DOCUMENT_BEGIN?FX_BUDGET:FX_EMPTY;
    else if(op==FX_DOCUMENT_BEGIN) {
        if(p->staging)result=FX_BUSY;
        else if(s->sequence==UINT64_MAX)result=FX_BUDGET;
        else {p->staging=fresh;fresh=0;p->received=0;p->token=++s->sequence;p->publisher=publisher;a->value[0]=p->token;}
    } else if(op==FX_DOCUMENT_CLEAR) {retired=p->current;p->current=0;}
    else if(!p->staging || !r.token || r.token!=p->token || publisher!=p->publisher)result=FX_STALE;
    else if(op==FX_DOCUMENT_WRITE) {
        if(r.offset!=p->received)result=FX_STALE;
        else if(r.bytes>p->staging->bytes-p->received)result=FX_BOUNDS;
        else {memcpy(p->staging->text+p->received,q->data+sizeof(r),r.bytes);p->received+=r.bytes;a->value[0]=p->received;}
    } else if(op==FX_DOCUMENT_COMMIT) {
        if(p->received!=p->staging->bytes)result=FX_STATE;
        else if(!utf8(p->staging->text,p->received))result=FX_INTEGRITY;
        else if(p->version==UINT32_MAX)result=FX_BUDGET;
        else {retired=p->current;p->current=p->staging;p->staging=0;p->current->version=++p->version;retain(p->current);p->token=p->publisher=0;a->value[0]=p->version;}
    } else if(op==FX_DOCUMENT_ABORT){retired=p->staging;p->staging=0;p->token=p->publisher=0;}
    else result=FX_ARGUMENT;
    unlock(s);fx_ui_document_release(retired);fx_ui_document_release(fresh);return result;
}
FxResult fx_ui_document_resource(void *context,uint32_t op,const FxNamedBinding *binding,FxPlatformOwner *o,uint64_t token,
    uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)binding;
    if(!o || !o->platform || !cookie || !token)return FX_ARGUMENT;
    Documents *s=state(o,op==FX_RESOURCE_ATTACH);if(!s)return op==FX_RESOURCE_ATTACH?FX_BUDGET:FX_STALE;
    if(op==FX_RESOURCE_ATTACH){if(s->revoked)return FX_STATE;*cookie=(uintptr_t)s;return FX_OK;}
    if(*cookie!=(uintptr_t)s)return FX_STALE;
    if(op==FX_RESOURCE_PANIC){if(!lock(s))return FX_BUSY;s->revoked=1u;
        for(uint32_t i=0u;i<FX_UI_CONTROLS;++i){retain(s->slots[i].current);retain(s->slots[i].staging);}unlock(s);return FX_OK;}
    if(op==FX_RESOURCE_DETACH) {
        FxUiDocument *retired[FX_UI_CONTROLS];uint32_t count=0;
        if(!lock(s))return FX_BUSY;
        for(uint32_t i=0;i<FX_UI_CONTROLS;++i) {
            DocumentSlot *p=&s->slots[i];
            if(p->staging && p->publisher==token){retired[count++]=p->staging;p->staging=0;p->token=p->publisher=0;}
        }
        unlock(s);for(uint32_t i=0;i<count;++i)fx_ui_document_release(retired[i]);*cookie=0;return FX_OK;
    }
    return op==FX_RESOURCE_CALL && q && a?call(s,token,q,a):FX_ARGUMENT;
}
