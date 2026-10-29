#include "fenestra_native_ui_dispatch.h"
_Static_assert(FX_NATIVE_UI_PAGES<=256u,"two retained page-route links cover one native view");

FxResult fx_ui_dispatch_register(FxNativeUiDispatch *d,FxPlatformOwner *owner) {
    if(!d || !owner || !owner->owner)return FX_ARGUMENT;
    uintptr_t domain=(uintptr_t)d,current=atomic_load_explicit(&owner->ui_dispatch_domain,memory_order_acquire);
    if(current==domain)return FX_OK;
    if(current)return current==(domain|1u)?FX_BUSY:FX_STALE;
    if(!atomic_compare_exchange_strong_explicit(&owner->ui_dispatch_domain,&current,domain|1u,
            memory_order_acq_rel,memory_order_acquire))return FX_BUSY;
    uintptr_t head=atomic_load_explicit(&d->owners,memory_order_acquire);
    for(;;) {
        for(FxPlatformOwner *p=(void *)head;p;p=(void *)p->ui_dispatch_registered_next)
            if(p->owner==owner->owner && p->generation==owner->generation) {
                atomic_store_explicit(&owner->ui_dispatch_domain,0u,memory_order_release);return FX_STALE;
            }
        owner->ui_dispatch_registered_next=head;
        if(atomic_compare_exchange_weak_explicit(&d->owners,&head,(uintptr_t)owner,
                memory_order_acq_rel,memory_order_acquire))break;
    }
    atomic_store_explicit(&owner->ui_dispatch_domain,domain,memory_order_release);return FX_OK;
}

uint32_t fx_ui_dispatch_span_links(uint32_t first,uint32_t count) {
    if(!first || first>65535u || !count || count>65536u-first)return 0u;
    return ((first+count-1u)>>8)-(first>>8)+1u;
}
static FxResult bind_span(FxNativeUiDispatch *d,FxNativeUi *u,FxUiPageSpan *span,FxUiPageRoute *links,
                         uint32_t capacity,uint32_t first,uint32_t count,uint32_t logical,uint32_t primary) {
    uint32_t needed=fx_ui_dispatch_span_links(first,count);
    if(!d || !u || !u->owner || !span || !links || !needed || capacity<needed || logical>UINT32_MAX-(count-1u))return FX_ARGUMENT;
    if(atomic_load_explicit(&u->owner->ui_dispatch_domain,memory_order_acquire)!=(uintptr_t)d)return FX_STATE;
    uintptr_t domain=atomic_load_explicit(&span->domain,memory_order_acquire);
    if(domain)return domain==(uintptr_t)d && span->ui==u && span->first==first && span->count==count && span->logical==logical?FX_OK:FX_STALE;
    uintptr_t view_domain=atomic_load_explicit(&u->route_domain,memory_order_acquire);
    if(view_domain && view_domain!=(uintptr_t)d)return FX_STALE;
    if(!primary && !view_domain)return FX_STATE;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong_explicit(&d->page_writer,&zero,1u,memory_order_acquire,memory_order_relaxed))return FX_BUSY;
    /* Recheck after taking the writer: another creator may have completed
     * this exact span while this caller was arriving. Never edit a live link. */
    domain=atomic_load_explicit(&span->domain,memory_order_acquire);
    if(domain) {
        atomic_store_explicit(&d->page_writer,0u,memory_order_release);
        return domain==(uintptr_t)d && span->ui==u && span->first==first && span->count==count && span->logical==logical?FX_OK:FX_STALE;
    }
    FxResult result=FX_OK;
    for(uint32_t i=0u;i<needed;++i)
        if(atomic_load_explicit(&links[i].domain,memory_order_acquire))result=FX_STALE;
    uint32_t begin=first>>8,last=(first+count-1u)>>8;
    for(uint32_t block=begin;result==FX_OK && block<=last;++block)
        for(FxUiPageRoute *r=(void *)atomic_load_explicit(&d->page_routes[block],memory_order_acquire);r;r=r->next) {
            const FxUiPageSpan *old=r->span;
            if(first<old->first+old->count && old->first<first+count){result=FX_INTEGRITY;break;}
        }
    if(result==FX_OK && primary && d->text_regions)
        result=fx_native_text_regions_add(d->text_regions,&u->text_region,u,sizeof(*u));
    if(result==FX_OK) {
        span->ui=u;span->first=first;span->count=count;span->logical=logical;span->reserved=0u;
        for(uint32_t block=begin;block<=last;++block) {
            FxUiPageRoute *r=&links[block-begin];r->ui=u;r->span=span;
            r->next=(void *)atomic_load_explicit(&d->page_routes[block],memory_order_acquire);
            atomic_store_explicit(&r->domain,(uintptr_t)d,memory_order_release);
            atomic_store_explicit(&d->page_routes[block],(uintptr_t)r,memory_order_release);
        }
        /* One final publication covers ALL bucket links. Lookup cannot expose
         * a half-bound span to a concurrent native callback. */
        atomic_store_explicit(&span->domain,(uintptr_t)d,memory_order_release);
        if(primary) {
            u->text_regions=d->text_regions;
            u->route_next=atomic_load_explicit(&d->pages,memory_order_acquire);
            atomic_store_explicit(&u->route_domain,(uintptr_t)d,memory_order_release);
            atomic_store_explicit(&d->pages,(uintptr_t)u,memory_order_release);
        }
    }
    atomic_store_explicit(&d->page_writer,0u,memory_order_release);return result;
}
FxResult fx_ui_dispatch_bind_page(FxNativeUiDispatch *d,FxNativeUi *u) {
    if(!u)return FX_ARGUMENT;
    uint32_t count=u->picker_page==u->page_base+1u?2u:FX_NATIVE_UI_PAGES;
    if(u->picker_page && u->picker_page!=u->page_base+1u && u->picker_page!=u->page_base+FX_NATIVE_UI_PAGES-1u)return FX_ARGUMENT;
    return bind_span(d,u,&u->page_span,u->page_routes,2u,u->page_base,count,0u,1u);
}
FxResult fx_ui_dispatch_bind_span(FxNativeUiDispatch *d,FxNativeUi *u,FxUiPageSpan *span,FxUiPageRoute *links,
                                uint32_t capacity,uint32_t first,uint32_t count,uint32_t logical) {
    return bind_span(d,u,span,links,capacity,first,count,logical,0u);
}
FxNativeUi *fx_ui_dispatch_resolve_page(const FxNativeUiDispatch *d,uint32_t page,uint32_t *logical) {
    if(!d || !page || page>65535u)return 0;
    for(FxUiPageRoute *r=(void *)atomic_load_explicit(&d->page_routes[page>>8],memory_order_acquire);r;r=r->next) {
        const FxUiPageSpan *span=r->span;
        if(atomic_load_explicit(&span->domain,memory_order_acquire)!=(uintptr_t)d)continue;
        FxNativeUi *u=span->ui;
        if(atomic_load_explicit(&u->route_domain,memory_order_acquire)==(uintptr_t)d &&
           page>=span->first && page-span->first<span->count) {
            if(logical)*logical=span->logical+page-span->first;
            return u;
        }
    }
    return 0;
}
FxNativeUi *fx_ui_dispatch_find_page(const FxNativeUiDispatch *d,uint32_t page) {
    return fx_ui_dispatch_resolve_page(d,page,0);
}

static void enqueue(FxNativeUiDispatch *d,FxPlatformOwner *owner) {
    if(atomic_exchange_explicit(&owner->ui_dispatch_queued,1u,memory_order_acq_rel))return;
    /* Count before publication: the consumer cannot decrement an uncounted
     * node. Count with no ready head temporarily means a producer in flight. */
    atomic_fetch_add_explicit(&d->pending,1u,memory_order_relaxed);
    uintptr_t head=atomic_load_explicit(&d->ready,memory_order_acquire);
    do {owner->ui_dispatch_ready_next=head;}
    while(!atomic_compare_exchange_weak_explicit(&d->ready,&head,(uintptr_t)owner,
                memory_order_acq_rel,memory_order_acquire));
}
FxResult fx_ui_dispatch_request(FxNativeUiDispatch *d,FxPlatformOwner *owner) {
    if(!d || !owner || !owner->owner)return FX_ARGUMENT;
    if(atomic_load_explicit(&owner->ui_dispatch_domain,memory_order_acquire)!=(uintptr_t)d)return FX_STATE;
    enqueue(d,owner);atomic_fetch_add_explicit(&d->submitted,1u,memory_order_relaxed);return FX_OK;
}
void fx_ui_dispatch_capture(FxNativeUiDispatch *d,uintptr_t task) {
    if(d && task)atomic_store_explicit(&d->task,task,memory_order_release);
}
FxResult fx_ui_dispatch_poll(FxNativeUiDispatch *d,uintptr_t task,int visible,FxResult (*flush)(FxPlatformOwner *)) {
    if(!d || !task || !visible || !flush || task!=atomic_load_explicit(&d->task,memory_order_acquire))return FX_EMPTY;
    uint32_t zero=0;if(!atomic_compare_exchange_strong(&d->running,&zero,1u))return FX_BUSY;
    if(!d->backlog)d->backlog=atomic_exchange_explicit(&d->ready,0u,memory_order_acq_rel);
    FxResult result=FX_OK;
    /* Each detached wave is finite; new producers and BUSY retries wait for
     * the next wave. No idle-owner scan, list reversal, allocation or slot cap.
     * Take next BEFORE clearing queued: producers can then immediately reuse
     * this owner's ready_next while the current flush is in progress. */
    for(uint32_t i=0;i<FX_UI_DISPATCH_BATCH && d->backlog;++i) {
        FxPlatformOwner *owner=(void *)d->backlog;d->backlog=owner->ui_dispatch_ready_next;
        (void)atomic_exchange_explicit(&owner->ui_dispatch_queued,0u,memory_order_acq_rel);
        atomic_fetch_sub_explicit(&d->pending,1u,memory_order_relaxed);
        atomic_store(&d->last_task,task);FxResult r=flush(owner);
        if(r!=FX_OK && r!=FX_EMPTY){enqueue(d,owner);result=r;}
        else atomic_fetch_add_explicit(&d->completed,1u,memory_order_relaxed);
    }
    atomic_store_explicit(&d->running,0u,memory_order_release);return result;
}
uint32_t fx_ui_dispatch_wait(const FxNativeUiDispatch *d,uintptr_t task,int visible,uint32_t original,uint32_t interval) {
    if(!d || !task || !visible || !interval || task!=atomic_load_explicit(&d->task,memory_order_acquire))return original;
    return original>interval?interval:original;
}
