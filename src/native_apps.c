#include "fenestra_native_apps.h"
#include "fenestra_memory.h"
struct FxNativeAppLink {
    FxNativeAppLink *next,*owner_next;uint32_t instance,page;FxNativeEvent events[FX_NATIVE_EVENTS];
};
void fx_native_apps_account_retained(FxNativeApps *apps) {
    if(!apps)return;
    if(!apps->embedded)fx_retained_account(apps->account,&apps->accounted,(uint32_t)sizeof(*apps));
    for(FxNativeAppFrame *f=atomic_load_explicit(&apps->frames,memory_order_seq_cst);f;f=f->next)
        fx_retained_account(apps->account,&f->accounted,(uint32_t)sizeof(*f));
}
#if defined(__aarch64__)
__asm__(".section .text.fx_native_apps_gate,\"ax\"\n.balign 4\n.global fx_native_apps_gate\n"
    ".type fx_native_apps_gate,%function\nfx_native_apps_gate:\n mov x0,x20\n mov x1,x22\n"
    " b fx_native_apps_condition\n.size fx_native_apps_gate,.-fx_native_apps_gate\n.previous\n");
extern uint32_t fx_native_apps_gate(void);
#else
static uint32_t fx_native_apps_gate(void){return 0u;}
#endif
static uint32_t noop(void *p){(void)p;return 1u;}
static FxNativeAppFrame *frame(FxNativeApps *apps,const FxNativePage *page) {
    if(!apps || !page)return 0;
    uint32_t id=page->identity>>16;if(!fx_native_apps_is_page(id))return 0;
    uint32_t ordinal=id==FX_NATIVE_WONDER_PAGE?0u:id-FX_NATIVE_APPLICATION_PAGE_BASE+1u;
    FxNativeAppFrame *f=apps->current[ordinal];
    return f && f->current && page->identity==f->native.page.identity &&
        page->descriptor==(uintptr_t)&f->native.descriptor?f:0;
}
static uint32_t opened(void *page) {
    FxNativeApps *apps=fx_native_apps_domain();FxNativeAppFrame *f=frame(apps,page);
    if(!f)return 0u;
    f->pinned=1u;
    if(apps->capture_task)apps->capture_task();
    return ((uint32_t (*)(void *))apps->callbacks[0])(page);
}
static uint32_t closed(void *page) {
    FxNativeApps *apps=fx_native_apps_domain();FxNativeAppFrame *f=frame(apps,page);
    if(f) {
        f->pinned=0u;
        if(apps->navigation_source==(f->native.page.identity>>16))atomic_store(&apps->navigation_pending,0u);
        if(f->notice)f->failed_selection=0u;
    }
    return apps?((uint32_t (*)(void *))apps->callbacks[1])(page):1u;
}
FxResult fx_native_apps_init(FxNativeApps *apps,const FxNativeMenuProfile *profile,const uintptr_t callbacks[5],
        void *context,void *(*allocate)(uint32_t),uint32_t (*count)(void *),
        FxResult (*entry)(void *,uint32_t,const FxNativePage *,int,FxNativeAppEntry *),
        FxResult (*action)(void *,uint32_t),uint32_t title,uint32_t previous,uint32_t next,uint32_t empty,uint32_t back,
        uint32_t unavailable,uint32_t retry) {
    if(!apps || !profile || !callbacks || !allocate || !count || !entry || !action || !unavailable || !retry)return FX_ARGUMENT;
    if(apps->allocate)return FX_BUSY;
    memset(apps,0,sizeof(*apps));apps->profile=*profile;memcpy(apps->callbacks,callbacks,sizeof(apps->callbacks));
    apps->context=context;apps->allocate=allocate;apps->count=count;apps->entry=entry;apps->action=action;
    apps->title=title;apps->previous_text=previous;apps->next_text=next;apps->empty_text=empty;apps->back_text=back;
    apps->unavailable_text=unavailable;apps->retry_text=retry;
    return FX_OK;
}
static FxNativeAppLink *link_for(FxNativeApps *apps,uint32_t instance,uint32_t page,FxNativeAppLink **head,FxAllocationObserver observer) {
    if(!head)return 0;
    for(FxNativeAppLink *link=*head;link;link=link->owner_next)
        if(link->instance==instance && link->page==page)return link;
    FxNativeEvent events[FX_NATIVE_EVENTS]={0};
    if(fx_native_menu_link(events,apps->profile.navigation,apps->profile.terminal,
            apps->profile.select_events,(uintptr_t)noop,page)!=FX_OK)return 0;
    FxNativeAppLink *link=apps->allocate((uint32_t)sizeof(*link));if(!link)return 0;
    fx_allocation_observe(observer,(uint32_t)sizeof(*link),FX_ALLOC_REQUESTED);
    memset(link,0,sizeof(*link));link->instance=instance;link->page=page;
    memcpy(link->events,events,sizeof(events));
    for(uint32_t i=0;i<FX_NATIVE_EVENTS;++i) {
        uint32_t event=(uint32_t)link->events[i].event&65535u;if(event==apps->profile.terminal)break;
        if(event==apps->profile.back_event)link->events[i].next=apps->profile.back;
        for(uint32_t k=0;k<4u;++k)if(event==apps->profile.select_events[k])
            link->events[i].condition=(uintptr_t)fx_native_apps_gate;
    }
    link->owner_next=*head;*head=link;link->next=apps->links;apps->links=link;
    fx_allocation_observe(observer,(uint32_t)sizeof(*link),FX_ALLOC_PUBLISHED);return link;
}
static FxNativePage *failed(FxNativeApps *apps,FxNativeAppFrame *f,uint32_t page,FxResult error) {
    apps->error=(uint32_t)error;
    if(error!=FX_BUDGET)return &f->native.page;
    /* A frame is changed only after native close. No additional storage is
     * required when constructing the error UI itself. Published owner event
     * records remain retained, but no longer appear in this descriptor. */
    FxNativeEntry rows[3]={
        {apps->unavailable_text,0x7b13u,0u,0u,(uintptr_t)noop,apps->profile.stay},
        {apps->retry_text,0x7b14u,1u,0u,(uintptr_t)noop,page},
        {apps->back_text,0x7b12u,0u,0u,(uintptr_t)noop,apps->profile.back}};
    uintptr_t callbacks[5];memcpy(callbacks,apps->callbacks,sizeof(callbacks));
    callbacks[0]=(uintptr_t)opened;callbacks[1]=(uintptr_t)closed;
    memset(&f->native,0,sizeof(f->native));
    FxResult r=fx_native_menu_build(&f->native,&apps->profile,page,apps->title,rows,3u,callbacks);
    if(r!=FX_OK){apps->error=(uint32_t)r;return 0;}
    for(uint32_t e=0u;e<FX_NATIVE_EVENTS;++e) {
        uint32_t code=(uint32_t)f->native.handlers[1][e].event&65535u;
        if(code==apps->profile.terminal)break;
        for(uint32_t k=0u;k<4u;++k)if(code==apps->profile.select_events[k])
            f->native.handlers[1][e].condition=(uintptr_t)fx_native_apps_gate;
    }
    f->native.page.selected=1u;f->notice=f->prepared=1u;return &f->native.page;
}
static FxNativeAppFrame *reserve_frame(FxNativeApps *apps,uint32_t ordinal) {
    FxNativeAppFrame *f=apps->current[ordinal];if(f)return f;
    f=ordinal?apps->allocate((uint32_t)sizeof(*f)):&apps->root;
    if(!f){apps->error=FX_BUDGET;return 0;}
    memset(f,0,sizeof(*f));f->ordinal=ordinal;
    if(ordinal) {
        f->next=atomic_load_explicit(&apps->frames,memory_order_relaxed);
        atomic_store_explicit(&apps->frames,f,memory_order_seq_cst);
        fx_retained_account(apps->account,&f->accounted,(uint32_t)sizeof(*f));
    }
    uintptr_t callbacks[5];memcpy(callbacks,apps->callbacks,sizeof(callbacks));
    callbacks[0]=(uintptr_t)opened;callbacks[1]=(uintptr_t)closed;
    FxNativeEntry back={apps->back_text,0x7b12u,0u,0u,(uintptr_t)noop,apps->profile.back};
    FxResult r=fx_native_menu_build(&f->native,&apps->profile,fx_native_apps_page_id(ordinal),apps->title,&back,1u,callbacks);
    if(r!=FX_OK){apps->error=(uint32_t)r;return 0;}
    f->current=1u;apps->current[ordinal]=f;return f;
}
FxNativePage *fx_native_apps_page(FxNativeApps *apps,uint32_t page) {
    if(!apps || !apps->allocate || !fx_native_apps_is_page(page))return 0;
    uint32_t ordinal=page==FX_NATIVE_WONDER_PAGE?0u:page-FX_NATIVE_APPLICATION_PAGE_BASE+1u;
    FxNativeAppFrame *old=apps->current[ordinal];
    if(old && old->pinned)return &old->native.page;
    apps->error=0u;
    /* The page object stays stable across closed-page rebuilds. Module UIs
     * may retain it as native touch ancestry. Immutable per-instance event
     * tables prevent a retired row callback from being retargeted. */
    FxNativeAppFrame *f=reserve_frame(apps,ordinal);if(!f)return 0;
    if(f->pending_error) {
        FxResult error=(FxResult)f->pending_error;f->pending_error=0u;return failed(apps,f,page,error);
    }
    uintptr_t callbacks[5];memcpy(callbacks,apps->callbacks,sizeof(callbacks));
    callbacks[0]=(uintptr_t)opened;callbacks[1]=(uintptr_t)closed;
    FxNativeEntry rows[FX_NATIVE_APP_ROWS+3u];
    uint32_t indices[FX_NATIVE_APP_ROWS]={0},items=0u;
    FxNativeAppEntry entries[FX_NATIVE_APP_ROWS]={0};
    uint32_t available=0u,begin=ordinal*FX_NATIVE_APP_ROWS,limit=apps->count(apps->context),more=0u;
    for(uint32_t i=0;i<limit;++i) {
        FxNativeAppEntry item={0};FxResult r=apps->entry(apps->context,i,0,0,&item);
        if(r==FX_EMPTY)continue;
        if(r!=FX_OK)return failed(apps,f,page,r);
        if(available>=FX_NATIVE_UI_OWNERS)return failed(apps,f,page,FX_BUDGET);
        if(available>=begin && items<FX_NATIVE_APP_ROWS) {
            r=apps->entry(apps->context,i,&f->native.page,0,&item);if(r!=FX_OK)return failed(apps,f,page,r);
            indices[items]=i;entries[items]=item;++items;
        }
        ++available;
        if(available>begin+FX_NATIVE_APP_ROWS){more=1u;break;}
    }
    if(more && ordinal+1u==FX_NATIVE_APP_PAGE_COUNT)return failed(apps,f,page,FX_BUDGET);
    /* Every reachable Next destination has retained storage before native
     * input can close this source. Allocation failure stays on this frame. */
    if(more && !reserve_frame(apps,ordinal+1u))return failed(apps,f,page,(FxResult)apps->error);
    if(old && old->prepared && !old->notice && old->count==items && old->more==more && !memcmp(old->indices,indices,items*sizeof(uint32_t)) &&
       !memcmp(old->entries,entries,items*sizeof(FxNativeAppEntry)))return &old->native.page;
    FxNativeAppLink *links[FX_NATIVE_APP_ROWS]={0};
    for(uint32_t i=0u;i<items;++i) {
        links[i]=link_for(apps,indices[i],entries[i].page,entries[i].links,entries[i].observer);
        if(!links[i])return failed(apps,f,page,FX_BUDGET);
    }
    f->notice=0u;f->failed_selection=0u;f->prepared=1u;f->count=items;f->more=more;memcpy(f->indices,indices,sizeof(indices));memcpy(f->entries,entries,sizeof(entries));
    uint32_t count=0u;
    for(uint32_t i=0;i<f->count;++i)
        rows[count++]=(FxNativeEntry){f->entries[i].text,0x7b00u+i,f->entries[i].page!=apps->profile.stay,
            f->entries[i].value!=0u,(uintptr_t)noop,f->entries[i].page};
    if(!count)rows[count++]=(FxNativeEntry){apps->empty_text,0x7b00u,0u,0u,(uintptr_t)noop,apps->profile.stay};
    if(ordinal)rows[count++]=(FxNativeEntry){apps->previous_text,0x7b10u,1u,0u,(uintptr_t)noop,fx_native_apps_page_id(ordinal-1u)};
    if(more && ordinal+1u<FX_NATIVE_APP_PAGE_COUNT)
        rows[count++]=(FxNativeEntry){apps->next_text,0x7b11u,1u,0u,(uintptr_t)noop,fx_native_apps_page_id(ordinal+1u)};
    rows[count++]=(FxNativeEntry){apps->back_text,0x7b12u,0u,0u,(uintptr_t)noop,apps->profile.back};
    memset(&f->native,0,sizeof(f->native));
    if(fx_native_menu_build(&f->native,&apps->profile,page,apps->title,rows,count,callbacks)!=FX_OK)return 0;
    for(uint32_t i=0;i<f->count;++i)f->native.events[i]=(uintptr_t)links[i]->events;
    return &f->native.page;
}
static void request_navigation(FxNativeApps *apps,const FxNativePage *page,uint32_t instance,uint32_t target) {
    apps->navigation_source=page->identity>>16;apps->navigation_instance=instance;apps->navigation_page=target;
    atomic_store_explicit(&apps->navigation_pending,1u,memory_order_release);
}
FxResult fx_native_apps_prepare_navigation(FxNativeApps *apps,const FxNativePage *page,uint32_t *destination) {
    if(!apps || !destination)return FX_ARGUMENT;
    *destination=0u;if(!fx_native_apps_pending(apps))return FX_EMPTY;
    FxNativeAppFrame *f=frame(apps,page);
    if(!f || !f->pinned || (page->identity>>16)!=apps->navigation_source) {
        atomic_store(&apps->navigation_pending,0u);return FX_EMPTY;
    }
    FxNativeAppEntry current={0};
    FxResult result=apps->entry(apps->context,apps->navigation_instance,page,1,&current);
    if(result==FX_BUSY)return result;
    atomic_store(&apps->navigation_pending,0u);
    if(result==FX_BUDGET) {
        apps->error=FX_BUDGET;f->failed_selection=1u;f->failed_instance=apps->navigation_instance;
        f->failed_page=apps->navigation_page;
        if(f->notice)return FX_EMPTY; /* Repeated failure keeps the same live error page. */
        f->pending_error=FX_BUDGET;*destination=apps->navigation_source;return FX_OK;
    }
    if(result==FX_OK && current.enabled && current.page==apps->navigation_page) {
        f->failed_selection=0u;apps->error=0u;*destination=current.page;return FX_OK;
    }
    /* Removed/rebound owners are never retargeted through an old link. A
     * stale error Retry returns to a newly copied Apps list instead. */
    f->failed_selection=0u;apps->error=(uint32_t)result;
    if(f->notice){*destination=apps->navigation_source;return FX_OK;}
    return FX_EMPTY;
}
uint32_t fx_native_apps_condition(const FxNativePage *page,const FxNativeEvent *event) {
    FxNativeApps *apps=fx_native_apps_domain();FxNativeAppFrame *f=frame(apps,page);
    if(!f || !f->pinned || !event)return 0u;
    if(f->notice) {
        uintptr_t at=(uintptr_t)event,base=f->native.events[1];
        if(at<base || at>=base+FX_NATIVE_EVENTS*sizeof(*event) || (at-base)%sizeof(*event))return 0u;
        for(uint32_t k=0u;k<4u;++k)if((event->event&65535u)==apps->profile.select_events[k]) {
            if(!f->failed_selection)return 1u;
            request_navigation(apps,page,f->failed_instance,f->failed_page);return 0u;
        }
        return 0u;
    }
    for(uint32_t i=0;i<f->count;++i) {
        uintptr_t base=f->native.events[i],at=(uintptr_t)event;
        if(at<base || at>=base+FX_NATIVE_EVENTS*sizeof(*event) || (at-base)%sizeof(*event))continue;
        int select=0;for(uint32_t k=0;k<4u;++k)if((event->event&65535u)==apps->profile.select_events[k])select=1;
        if(!select)return 0u;
        if(f->entries[i].page!=apps->profile.stay) {
            request_navigation(apps,page,f->indices[i],f->entries[i].page);return 0u;
        }
        FxNativeAppEntry current={0};
        if(apps->entry(apps->context,f->indices[i],page,0,&current)!=FX_OK || !current.enabled ||
           current.page!=f->entries[i].page)return 0u;
        return current.page==apps->profile.stay?apps->action(apps->context,f->indices[i])==FX_OK:1u;
    }
    return 0u;
}
uint32_t fx_native_apps_value(FxNativeApps *apps,const FxNativePage *page,uint32_t row,int *enabled) {
    FxNativeAppFrame *f=frame(apps,page);if(!f || !enabled)return 0u;
    if(f->notice) {
        if(row==0x7b13u)*enabled=0;
        else if(row==0x7b14u)*enabled=1;
        return 0u;
    }
    if(row>=0x7b00u && row<0x7b00u+FX_NATIVE_APP_ROWS) {
        uint32_t i=row-0x7b00u;
        if(i>=f->count){*enabled=0;return 0u;}
        *enabled=f->entries[i].enabled!=0u;return f->entries[i].value;
    }
    return 0u;
}
