#include "fenestra_native_ui_pager.h"
#include "fenestra_native_ui_reader.h"
#include "fenestra_memory.h"
#define PAGER_TAG 65536u
typedef struct Pager Pager;
typedef struct Page Page;
typedef struct TextRoute {struct TextRoute *next;Page *page;} TextRoute;
struct Page {
    Pager *pager;Page *origin;uint32_t parent,ordinal,physical,text,index;
    _Atomic(FxNativeUi *) view;
    FxUiPageSpan span;FxUiPageRoute routes[2];
    uint32_t destinations[FX_UI_CONTROLS];
    Page *cache_next; /* GUI-task-only lookup link; never a native callback. */
    TextRoute text_routes[2]; /* Published only once this node has a concrete view. */
};
struct Pager {
    FxNativeUiSpace *space;FxNativeUi *root;
    _Atomic(Page **) pages;_Atomic uint32_t count;uint32_t capacity,used,peak;
    FxAllocationObserver observer;
    Page **cache;uint32_t cache_capacity,cache_rehashes;
    uint64_t lookups,probes;
    Page *inline_cache[16];
};
static const FxNativeUiSource source;
/* Read-only validation of actual retained views; no emulator-only UI path. */
const uint32_t fx_native_ui_pager_metadata[19]={0x46585047u,1u,
    offsetof(FxNativeUi,source_context),offsetof(Page,pager),offsetof(Pager,pages),offsetof(Pager,count),
    offsetof(Pager,capacity),offsetof(Page,physical),offsetof(Page,view),offsetof(Page,parent),
    offsetof(Page,ordinal),offsetof(Page,origin),sizeof(Page),sizeof(Pager),
    offsetof(FxNativeUi,controls),offsetof(FxNativeUi,count),offsetof(FxNativeUi,picker_page),
    sizeof(FxNativeUi),FX_NATIVE_UI_PAGER_BUDGET};

FxResult fx_native_ui_space_init(FxNativeUiSpace *s,FxNativeUiDispatch *domain,FxIndexMemory memory,
    void *context,FxResult (*ensure)(void *,uint32_t),uint32_t first,uint32_t limit,
    uint32_t text,uint32_t stride,uint32_t extra) {
    if(!s || !domain || !memory.allocate || !memory.release || !ensure || !first || first>=limit ||
       limit>65536u || stride<FX_NATIVE_UI_TEXTS+2u || stride>FX_NATIVE_TEXT_LIMIT ||
       extra<text || extra>FX_NATIVE_TEXT_LIMIT)return FX_ARGUMENT;
    if(s->domain)return s->domain==domain && s->first==first && s->limit==limit &&
        s->text_base==text && s->text_stride==stride && s->extra_text_base==extra &&
        s->text_context==context && s->ensure_text==ensure && s->memory.context==memory.context &&
        s->memory.allocate==memory.allocate && s->memory.release==memory.release?FX_OK:FX_STALE;
    s->memory=memory;s->text_context=context;s->ensure_text=ensure;s->first=first;s->limit=limit;
    s->text_base=text;s->text_stride=stride;s->extra_text_base=extra;atomic_store(&s->next,first);
    atomic_store(&s->next_text,extra);
    s->domain=domain;return FX_OK;
}
FxResult fx_native_ui_space_info(const FxNativeUiSpace *s,FxNativeUiSpaceInfo *out) {
    if(!s || !s->domain || !out)return FX_ARGUMENT;
    *out=(FxNativeUiSpaceInfo){1u,sizeof(*out),s->first,s->limit,atomic_load_explicit(&s->next,memory_order_acquire),
        s->extra_text_base,FX_NATIVE_TEXT_LIMIT,atomic_load_explicit(&s->next_text,memory_order_acquire)};
    return FX_OK;
}
FxResult fx_native_ui_space_take(FxNativeUiSpace *s,uint32_t count,uint32_t *first) {
    if(!s || !s->domain || !first || !count)return FX_ARGUMENT;
    uint32_t next=atomic_load_explicit(&s->next,memory_order_relaxed);
    for(;;) {
        if(next<s->first || next>s->limit || count>s->limit-next){fx_capacity_failed(s->page_capacity,FX_BUDGET);return FX_BUDGET;}
        if(atomic_compare_exchange_weak_explicit(&s->next,&next,next+count,memory_order_acq_rel,memory_order_relaxed)){
            FxCapacityClaim claim=fx_capacity_acquire(s->page_capacity,count);fx_capacity_retain(&claim);
            *first=next;return FX_OK;
        }
    }
}
void fx_native_ui_space_rollback(FxNativeUiSpace *s,uint32_t first,uint32_t count) {
    if(!s || !count || first<s->first || first>s->limit || count>s->limit-first)return;
    /* Native callbacks may retain any registered ID, including the tail.
     * Tail position alone is not proof that a reservation is unpublished. */
    for(uint32_t i=0u;i<count;++i)if(fx_ui_dispatch_find_page(s->domain,first+i))return;
    uint32_t expected=first+count;
    if(atomic_compare_exchange_strong_explicit(&s->next,&expected,first,memory_order_acq_rel,memory_order_relaxed)) {
        FxCapacityClaim claim={s->page_capacity,count,1u};fx_capacity_release(&claim,0u);
    }
}
FxResult fx_native_ui_space_observe(FxNativeUiSpace *s,FxCapacityDomain *pages,FxCapacityDomain *texts) {
    if(!s || !s->domain || !pages || !texts || !pages->registry || pages->registry!=texts->registry ||
       atomic_load_explicit(&pages->registry->ready,memory_order_acquire) ||
       pages->limit!=s->limit-s->first || texts->limit!=FX_NATIVE_TEXT_LIMIT-s->extra_text_base)return FX_ARGUMENT;
    if(s->page_capacity || s->text_capacity)return s->page_capacity==pages && s->text_capacity==texts?FX_OK:FX_STALE;
    uint32_t count=atomic_load(&s->next)-s->first,text=atomic_load(&s->next_text)-s->extra_text_base;
    FxResult r=fx_capacity_seed(pages,count,count);if(r!=FX_OK)return r;
    r=fx_capacity_seed(texts,text,text);if(r!=FX_OK)return r;
    s->page_capacity=pages;s->text_capacity=texts;return FX_OK;
}
static void *allocate(Pager *p,uint32_t bytes) {
    if(p->used>FX_NATIVE_UI_PAGER_BUDGET || bytes>FX_NATIVE_UI_PAGER_BUDGET-p->used)return 0;
    void *memory=p->space->memory.allocate(p->space->memory.context,bytes);
    if(memory) {
        p->used+=bytes;if(p->peak<p->used)p->peak=p->used;
        fx_allocation_observe(p->observer,bytes,FX_ALLOC_REQUESTED);memset(memory,0,bytes);
    }
    return memory;
}
static void keep(Pager *p,uint32_t bytes){fx_allocation_observe(p->observer,bytes,FX_ALLOC_PUBLISHED);}
static void release(Pager *p,void *memory,uint32_t bytes,uint32_t published) {
    if(!memory)return;
    p->space->memory.release(p->space->memory.context,memory);p->used-=bytes;
    fx_allocation_observe(p->observer,bytes,published?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);
}
static Page *context(FxNativeUi *u){return u?(Page *)u->source_context:0;}
static void *view_allocate(FxNativeUi *u,uint32_t bytes){return allocate(context(u)->pager,bytes);}
static void view_release(FxNativeUi *u,void *memory,uint32_t bytes,uint32_t published){release(context(u)->pager,memory,bytes,published);}

static uint32_t cache_bucket(Page *origin,uint32_t parent,uint32_t ordinal,uint32_t capacity) {
    uint64_t key=((uint64_t)parent<<32)|ordinal;
    key^=(uint64_t)(uintptr_t)origin;
    key^=key>>30;key*=UINT64_C(0xbf58476d1ce4e5b9);
    key^=key>>27;key*=UINT64_C(0x94d049bb133111eb);
    return (uint32_t)(key^(key>>31))&(capacity-1u);
}
/* Cold original UI task only. Native callbacks resolve published spans/index
 * positions directly. Cache links can therefore be rehashed and old buckets
 * freed here without retiring the native page or its read-side index. */
static Page *get_page(Pager *p,Page *origin,uint32_t parent,uint32_t ordinal) {
    uint32_t count=atomic_load_explicit(&p->count,memory_order_acquire);
    Page **old=atomic_load_explicit(&p->pages,memory_order_acquire);
    ++p->lookups;
    uint32_t bucket=cache_bucket(origin,parent,ordinal,p->cache_capacity);
    for(Page *at=p->cache[bucket];at;at=at->cache_next) {
        ++p->probes;
        if(at->origin==origin && at->parent==parent && at->ordinal==ordinal)return at;
    }
    if(count>(UINT32_MAX-PAGER_TAG)/2u)return 0;
    Page *page=allocate(p,(uint32_t)sizeof(*page));if(!page)return 0;
    uint32_t capacity=p->capacity;Page **index=old;
    if(count==capacity) {
        capacity=capacity?capacity*2u:8u;
        index=allocate(p,capacity*(uint32_t)sizeof(*index));
        if(!index){release(p,page,(uint32_t)sizeof(*page),0u);return 0;}
        if(count)memcpy(index,old,count*sizeof(*index));
    }
    uint32_t cache_capacity=p->cache_capacity;Page **cache=p->cache;
    if(count>=cache_capacity/2u) {
        cache_capacity*=2u;
        cache=allocate(p,cache_capacity*(uint32_t)sizeof(*cache));
        if(!cache)goto failed_cache;
    }
    uint32_t physical=0u;
    if(fx_native_ui_space_take(p->space,2u,&physical)!=FX_OK)goto failed;
    page->pager=p;page->origin=origin;page->parent=parent;page->ordinal=ordinal;
    page->physical=physical;page->index=count;
    if(fx_ui_dispatch_bind_span(p->space->domain,p->root,&page->span,page->routes,2u,
        physical,2u,PAGER_TAG+count*2u)!=FX_OK)goto failed_id;
    /* Span lookup can yield no concrete view until this final index publish;
     * no original UI caller can enter a destination before snapshot returns. */
    index[count]=page;
    if(cache!=p->cache) {
        for(uint32_t i=1u;i<count;++i) {
            Page *at=old[i];uint32_t b=cache_bucket(at->origin,at->parent,at->ordinal,cache_capacity);
            at->cache_next=cache[b];cache[b]=at;
        }
        keep(p,cache_capacity*(uint32_t)sizeof(*cache));
        if(p->cache!=p->inline_cache)release(p,p->cache,p->cache_capacity*(uint32_t)sizeof(*cache),1u);
        p->cache=cache;p->cache_capacity=cache_capacity;++p->cache_rehashes;
    }
    bucket=cache_bucket(origin,parent,ordinal,cache_capacity);
    page->cache_next=cache[bucket];cache[bucket]=page;
    if(index!=old) {
        keep(p,capacity*(uint32_t)sizeof(*index));p->capacity=capacity;
        atomic_store_explicit(&p->pages,index,memory_order_release);
        /* Old read-side index generations remain valid through reboot. */
    }
    keep(p,(uint32_t)sizeof(*page));atomic_store_explicit(&p->count,count+1u,memory_order_release);
    return page;
failed_id:
    fx_native_ui_space_rollback(p->space,physical,2u);
failed:
    if(cache!=p->cache)release(p,cache,cache_capacity*(uint32_t)sizeof(*cache),0u);
failed_cache:
    if(index!=old)release(p,index,capacity*(uint32_t)sizeof(*index),0u);
    release(p,page,(uint32_t)sizeof(*page),0u);return 0;
}
static Page *find_page(Pager *p,uint32_t physical) {
    Page **pages=atomic_load_explicit(&p->pages,memory_order_acquire);
    if(physical==p->root->page_base || physical==fx_native_ui_picker_id(p->root))return pages[0];
    uint32_t tag=0u;
    if(fx_ui_dispatch_resolve_page(p->space->domain,physical,&tag)!=p->root || tag<PAGER_TAG)return 0;
    uint32_t index=(tag-PAGER_TAG)/2u;
    uint32_t count=atomic_load_explicit(&p->count,memory_order_acquire);
    pages=atomic_load_explicit(&p->pages,memory_order_acquire);
    if(index>=count)return 0;
    Page *page=pages[index];return physical==page->physical || physical==page->physical+1u?page:0;
}
static FxNativeUi *resolve(FxNativeUi *u,uint32_t physical) {
    Page *node=context(u);Page *page=node?find_page(node->pager,physical):0;
    return page?atomic_load_explicit(&page->view,memory_order_acquire):0;
}
static FxResult window_snapshot(FxNativeUi *u,FxUiControl *out,uint32_t *count,uint32_t *revision,uint32_t *versions,uint8_t *order) {
    Page *page=context(u);Pager *p=page->pager;
    if(page->ordinal>UINT32_MAX/FX_NATIVE_UI_PAGE_ITEMS)return FX_BOUNDS;
    uint32_t children=0u,total=0u;
    uint32_t modal=u->pinned[FX_NATIVE_UI_PAGES-1u] || u->editing<u->count;
    FxResult r;
    if(modal) {
        r=fx_ui_snapshot_selected(u->owner,u->controls,u->count,out,versions,revision);
        if(r!=FX_OK)return r;
        memcpy(order,u->order,u->count);children=u->count-1u;
    } else {
        r=fx_ui_project_page(u->owner,page->parent,page->ordinal*FX_NATIVE_UI_PAGE_ITEMS,
            &out[0],&out[1],FX_NATIVE_UI_PAGE_ITEMS,&children,&total,revision,&versions[1],&order[1]);
        if(r!=FX_OK)return r;
    }
    out[0].flags|=FX_UI_ROOT;
    if(!out[0].id) {
        out[0].parent=UINT32_MAX;
        uint32_t n=0u;while(n<FX_UI_TEXT-1u && p->root->contribution_title[n]) {
            out[0].label[n]=(char)(p->root->contribution_title[n]<128u?p->root->contribution_title[n]:'?');++n;
        }
        out[0].label[n]=0;
    }
    versions[0]=*revision;
    if(!modal){order[0]=0u;for(uint32_t i=1u;i<=children;++i)++order[i];}
    uint32_t n=children+1u;
    if(!modal && page->ordinal*FX_NATIVE_UI_PAGE_ITEMS+children<total) {
        out[n]=(FxUiControl){.abi=1u,.bytes=sizeof(FxUiControl),.parent=out[0].id,
            .kind=FX_UI_PAGE,.flags=FX_UI_ENABLED|FX_UI_VISIBLE};
        memcpy(out[n].label,"Next page",10u);versions[n]=*revision;order[n]=(uint8_t)n;++n;
    }
    /* Resolve all destinations before any of these copied rows is published.
     * Condition callbacks only consume the resulting native event records. */
    for(uint32_t i=0u;i<n;++i) {
        uint32_t destination=(uint32_t)u->profile.stay;
        if(i && out[i].kind==FX_UI_PAGE) {
            Page *child=out[i].id?get_page(p,page,out[i].id,0u):get_page(p,page,out[0].id,page->ordinal+1u);
            if(!child)return FX_BUDGET;
            destination=child->physical;
        }
        page->destinations[i]=destination;
    }
    *count=n;return FX_OK;
}
static uint32_t destination(FxNativeUi *u,uint32_t slot) {
    return slot<FX_UI_CONTROLS?context(u)->destinations[slot]:(uint32_t)u->profile.stay;
}
static FxNativeUi *make_view(Page *page) {
    FxNativeUi *u=atomic_load_explicit(&page->view,memory_order_acquire);
    Pager *p=page->pager;
    if(u)return u;
    FxNativeUi *parent=page->origin?atomic_load_explicit(&page->origin->view,memory_order_acquire):0;
    if(!parent || !parent->pages[0])return 0;
    /* Prospective PAGE rows need a destination identity, not font roots.
     * Allocate text only for the concrete view which is actually opened. */
    FxNativeUiSpace *s=p->space;
    uint32_t text=atomic_load_explicit(&s->next_text,memory_order_relaxed),end=text+FX_NATIVE_UI_TEXTS+2u;
    if(text>FX_NATIVE_TEXT_LIMIT-(FX_NATIVE_UI_TEXTS+2u) ||
       s->ensure_text(s->text_context,end)!=FX_OK){fx_capacity_failed(s->text_capacity,FX_BUDGET);return 0;}
    u=allocate(p,(uint32_t)sizeof(*u));if(!u)return 0;
    FxNativeMenuProfile profile=p->root->profile;profile.page=&parent->pages[0]->page;profile.back=profile.page->identity>>16;
    if(fx_native_ui_init(u,p->root->owner,&profile,p->root->allocate,page->physical,text,p->root->row_base,
        p->root->actions,p->root->options,p->root->callbacks)!=FX_OK)goto failed;
    u->dialog=p->root->dialog;u->editor_profile=p->root->editor_profile;u->reader_profile=p->root->reader_profile;
    u->source_context=page;u->picker_page=page->physical+1u;u->observer=p->observer;
    u->input_ready=p->root->input_ready;u->text_regions=p->root->text_regions;
    u->request_refresh=p->root->request_refresh;
    memcpy(u->contribution_title,p->root->contribution_title,sizeof(u->contribution_title));
    if(u->text_regions && fx_native_text_regions_add(u->text_regions,&u->text_region,u,sizeof(*u))!=FX_OK)goto failed;
    u->source=&source;page->text=text;keep(p,(uint32_t)sizeof(*u));
    /* GUI writers serialize reservations. Font/text readers only see immutable
     * bucket links and an acquire-published view; no history-wide lookup. */
    uint32_t first=text>>10,last=(end-1u)>>10;
    for(uint32_t bucket=first;bucket<=last;++bucket) {
        TextRoute *route=&page->text_routes[bucket-first];route->page=page;
        route->next=(void *)atomic_load_explicit(&s->text_routes[bucket],memory_order_acquire);
        atomic_store_explicit(&s->text_routes[bucket],(uintptr_t)route,memory_order_release);
    }
    atomic_store_explicit(&s->next_text,end,memory_order_release);
    FxCapacityClaim claim=fx_capacity_acquire(s->text_capacity,FX_NATIVE_UI_TEXTS+2u);fx_capacity_retain(&claim);
    atomic_store_explicit(&page->view,u,memory_order_release);return u;
failed:
    release(p,u,(uint32_t)sizeof(*u),0u);return 0;
}
static FxNativePage *native_page(FxNativeUi *u,uint32_t physical) {
    Page *node=context(u);Page *page=node?find_page(node->pager,physical):0;
    if(!page)return 0;
    FxNativeUi *view=make_view(page);if(!view)return 0;
    if(page->origin) {
        uint32_t pinned=0u;for(uint32_t i=0u;i<FX_NATIVE_UI_PAGES;++i)pinned|=view->pinned[i];
        FxNativeUi *parent=atomic_load_explicit(&page->origin->view,memory_order_acquire);
        if(!pinned && parent && parent->pages[0]) {
            view->profile.page=&parent->pages[0]->page;view->profile.back=view->profile.page->identity>>16;
        }
    }
    return fx_native_ui_view_page(view,physical);
}
static void mark_return(FxNativeUi *view,uint64_t focus) {
    if(view){view->return_focus=focus;view->projection_pending=1u;}
}
static void window_closed(FxNativeUi *u,uint32_t slot) {
    Page *page=context(u);
    if(slot || !page->origin)return;
    FxNativeUi *parent=atomic_load_explicit(&page->origin->view,memory_order_acquire);
    mark_return(parent,page->ordinal?UINT64_C(2)<<32:u->root_control);
}
static FxResult retirement(FxNativeUi *u,uint32_t physical,uint32_t *destination_out) {
    uint32_t index=fx_native_ui_slot(u,physical);
    if(!u || !destination_out || index>=FX_NATIVE_UI_PAGES)return FX_ARGUMENT;
    uint32_t id=(uint32_t)u->focus_tokens[index];
    if(!id)id=index==FX_NATIVE_UI_PAGES-1u && u->editing<u->count?u->controls[u->editing].id:u->root_control;
    uint32_t slot;FxResult result=fx_ui_retired_parent_slot(u->owner,id,&slot);
    if(result==FX_EMPTY && !index && context(u)->ordinal) {
        FxUiControl parent;uint32_t n,total,revision;
        result=fx_ui_project_page(u->owner,context(u)->parent,0u,&parent,0,0u,&n,&total,&revision,0,0);
        if(result!=FX_OK)return result;
        uint32_t last=total?(total-1u)/FX_NATIVE_UI_PAGE_ITEMS:0u;
        if(context(u)->ordinal<=last)return FX_EMPTY;
        for(Page *at=context(u)->origin;at;at=at->origin) {
            FxNativeUi *view=atomic_load_explicit(&at->view,memory_order_acquire);
            if(at->ordinal<=last && view && view->root_control==u->root_control) {
                mark_return(view,UINT64_C(2)<<32);
                *destination_out=at->physical;return FX_OK;
            }
        }
        return FX_INTEGRITY;
    }
    if(result!=FX_OK)return result;
    Pager *p=context(u)->pager;
    if(slot==UINT32_MAX){*destination_out=p->root->profile.page->identity>>16;return FX_OK;}
    uint32_t parent_id=0u;
    if(slot) {
        FxUiControl c;uint32_t n,total,revision;
        result=fx_ui_snapshot_range(u->owner,slot-1u,&c,1u,&n,&total,&revision,0,0);
        if(result!=FX_OK)return result;
        if(n!=1u)return FX_INTEGRITY;
        parent_id=c.id;
    }
    for(Page *page=context(u);page;page=page->origin) {
        FxNativeUi *view=atomic_load_explicit(&page->view,memory_order_acquire);
        if(view && (view->root_control==parent_id || (!parent_id && page->index==0u))) {
            mark_return(view,id);
            *destination_out=page->physical;return FX_OK;
        }
    }
    mark_return(p->root,id);*destination_out=p->root->page_base;return FX_OK;
}
static const FxNativeUiSource source={view_allocate,view_release,window_snapshot,destination,native_page,resolve,retirement,window_closed};

FxResult fx_native_ui_pager_enable(FxNativeUi *u,FxNativeUiSpace *space) {
    if(!u || !u->owner || !space || !space->domain ||
       atomic_load_explicit(&u->route_domain,memory_order_acquire)!=(uintptr_t)space->domain)return FX_ARGUMENT;
    if(u->source)return u->source==&source?FX_OK:FX_STATE;
    for(uint32_t i=0u;i<FX_NATIVE_UI_PAGES;++i)if(u->pinned[i])return FX_BUSY;
    FxAllocationObserver observer=fx_platform_account_observer(u->owner,"ui.pager");
    Pager *p=space->memory.allocate(space->memory.context,(uint32_t)sizeof(*p));if(!p)return FX_BUDGET;
    memset(p,0,sizeof(*p));p->space=space;p->root=u;p->observer=observer;p->used=p->peak=(uint32_t)sizeof(*p);
    p->cache=p->inline_cache;p->cache_capacity=16u;
    fx_allocation_observe(observer,(uint32_t)sizeof(*p),FX_ALLOC_REQUESTED);
    Page *root=allocate(p,(uint32_t)sizeof(*root));Page **index=allocate(p,8u*(uint32_t)sizeof(*index));
    if(!root || !index) {
        if(root)release(p,root,(uint32_t)sizeof(*root),0u);
        if(index)release(p,index,8u*(uint32_t)sizeof(*index),0u);
        space->memory.release(space->memory.context,p);fx_allocation_observe(observer,(uint32_t)sizeof(*p),FX_ALLOC_FREE_PENDING);return FX_BUDGET;
    }
    root->pager=p;root->physical=u->page_base;root->text=u->text_base;atomic_store(&root->view,u);index[0]=root;
    p->capacity=8u;atomic_store(&p->pages,index);atomic_store(&p->count,1u);
    /* Detach only closed presentation metadata; native objects themselves
     * remain retained with their original accounting row and callback IDs. */
    fx_native_ui_reader_close(u);
    for(uint32_t i=0u;i<FX_NATIVE_UI_PAGES;++i){atomic_store(&u->pages[i],0);atomic_store(&u->page_accounted[i],0u);}
    u->count=0u;u->revision=0u;u->editing=UINT32_MAX;u->picker_active=0;u->picker_staged=0;
    u->source_context=root;u->observer=observer;
    keep(p,(uint32_t)sizeof(*p));keep(p,(uint32_t)sizeof(*root));keep(p,8u*(uint32_t)sizeof(*index));
    u->source=&source;return FX_OK;
}
FxResult fx_native_ui_pager_info(FxNativeUi *u,FxNativeUiPagerInfo *out) {
    if(!u || !out)return FX_ARGUMENT;
    if(u->source!=&source)return FX_EMPTY;
    Pager *p=context(u)->pager;
    *out=(FxNativeUiPagerInfo){1u,sizeof(*out),atomic_load_explicit(&p->count,memory_order_acquire),
        p->capacity,p->cache_capacity,p->cache_rehashes,p->used,p->peak,p->lookups,p->probes};
    return FX_OK;
}
int fx_native_ui_pager_text(FxNativeUiSpace *space,uint32_t id,const uint16_t **out,uint32_t *bytes) {
    if(!space || !space->domain || !out || !bytes || id<space->extra_text_base || id>=FX_NATIVE_TEXT_LIMIT)return 0;
    FxNativeUi *view=0;
    for(TextRoute *route=(void *)atomic_load_explicit(&space->text_routes[id>>10],memory_order_acquire);route;route=route->next) {
        Page *page=route->page;
        if(id>=page->text && id-page->text<FX_NATIVE_UI_TEXTS+2u) {
            view=atomic_load_explicit(&page->view,memory_order_acquire);break;
        }
    }
    static const uint16_t empty[]=u"";
    if(!view || view->text_base>id || id-view->text_base>=FX_NATIVE_UI_TEXTS+2u){*out=empty;*bytes=0u;return 1;}
    if(id-view->text_base>=FX_NATIVE_UI_TEXTS) {
        *out=view->contribution_title;uint32_t n=0u;while(n<FX_UI_TEXT && (*out)[n])++n;*bytes=n*2u;return 1;
    }
    return fx_native_ui_text(view,id,out,bytes);
}
