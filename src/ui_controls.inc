/* Internal logical UI storage. All readers/writers hold owner->ui_lock.
 * Allocated control addresses and IDs never move or recycle. Only private
 * directory/hash arrays are replaced, after every allocation has succeeded.
 * Native descriptors use copied snapshots, never these private arrays. */
#define UI_CONTROL_CHUNK 16u

typedef struct {
    FxUiControl control;
    uint32_t revision,order,next;
} UiControlNode;
typedef struct {UiControlNode nodes[UI_CONTROL_CHUNK];} UiControlBlock;
struct FxUiControlStore {
    FxIndexMemory memory;FxAllocationObserver observer;
    uint32_t used,peak,blocks,directory_capacity,bucket_count;
    UiControlBlock **directory;uint32_t *buckets;
    uint32_t inline_order[FX_UI_CONTROLS],inline_next[FX_UI_CONTROLS];
};

static UiControlNode *ui_control_node(const FxPlatformOwner *s,uint32_t index) {
    index-=FX_UI_CONTROLS;
    return &s->ui_controls->directory[index/UI_CONTROL_CHUNK]->nodes[index%UI_CONTROL_CHUNK];
}
static FxUiControl *ui_control_at(const FxPlatformOwner *s,uint32_t index) {
    return index<FX_UI_CONTROLS?(FxUiControl *)&s->controls[index]:&ui_control_node(s,index)->control;
}
static uint32_t *ui_control_revision(const FxPlatformOwner *s,uint32_t index) {
    return index<FX_UI_CONTROLS?(uint32_t *)&s->control_revisions[index]:&ui_control_node(s,index)->revision;
}
static uint32_t ui_control_order(const FxPlatformOwner *s,uint32_t index) {
    if(!s->ui_controls)return s->ui_order[index];
    return index<FX_UI_CONTROLS?s->ui_controls->inline_order[index]:ui_control_node(s,index)->order;
}
static void ui_control_set_order(FxPlatformOwner *s,uint32_t index,uint32_t value) {
    if(index<FX_UI_CONTROLS) {
        /* Compatibility shadow for old internal observation fixtures. Core
         * always reads the wide order after the first external block. */
        s->ui_order[index]=(uint8_t)value;
        if(s->ui_controls)s->ui_controls->inline_order[index]=value;
    } else ui_control_node(s,index)->order=value;
}
static uint32_t *ui_control_next(FxPlatformOwner *s,uint32_t index) {
    return index<FX_UI_CONTROLS?&s->ui_controls->inline_next[index]:&ui_control_node(s,index)->next;
}
static uint32_t ui_control_hash(uint32_t x) {
    x^=x>>16;x*=UINT32_C(0x7feb352d);x^=x>>15;x*=UINT32_C(0x846ca68b);return x^(x>>16);
}
static uint32_t ui_control_index(FxPlatformOwner *s,uint32_t id) {
    if(!s->ui_controls) {
        for(uint32_t i=0u;i<s->control_count;++i)if(s->controls[i].id==id)return i;
    } else {
        FxUiControlStore *store=s->ui_controls;
        for(uint32_t at=store->buckets[ui_control_hash(id)&(store->bucket_count-1u)];at;) {
            uint32_t index=at-1u;
            if(ui_control_at(s,index)->id==id)return index;
            at=*ui_control_next(s,index);
        }
    }
    return UINT32_MAX;
}
static void ui_control_insert(FxPlatformOwner *s,uint32_t index) {
    FxUiControlStore *store=s->ui_controls;
    if(store) {
        uint32_t bucket=ui_control_hash(ui_control_at(s,index)->id)&(store->bucket_count-1u);
        *ui_control_next(s,index)=store->buckets[bucket];store->buckets[bucket]=index+1u;
    }
}
static void *ui_controls_allocate(FxPlatformOwner *s,FxUiControlStore *store,uint32_t bytes) {
    if(store->used>s->ui_control_budget || bytes>s->ui_control_budget-store->used)return 0;
    void *p=store->memory.allocate(store->memory.context,bytes);
    if(p) {
        store->used+=bytes;if(store->peak<store->used)store->peak=store->used;
        fx_allocation_observe(store->observer,bytes,FX_ALLOC_REQUESTED);
    }
    return p;
}
static void ui_controls_release(FxUiControlStore *store,void *p,uint32_t bytes,uint32_t published) {
    store->memory.release(store->memory.context,p);store->used-=bytes;
    fx_allocation_observe(store->observer,bytes,published?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);
}
static uint32_t ui_controls_slots(const FxPlatformOwner *s) {
    return FX_UI_CONTROLS+(s->ui_controls?s->ui_controls->blocks*UI_CONTROL_CHUNK:0u);
}
/* Normal worker only. Native input, focus, polling and retirement never
 * allocate. The simultaneous old/new index cost is included in the budget. */
static FxResult ui_controls_grow(FxPlatformOwner *s) {
    if(s->control_count<ui_controls_slots(s))return FX_OK;
    FxUiControlStore *store=s->ui_controls;uint32_t fresh=store==0;
    if(fresh) {
        FxIndexMemory memory=s->ui_control_memory;
        if(!memory.allocate || !memory.release)return FX_BUDGET;
        if(sizeof(*store)>s->ui_control_budget)return FX_BUDGET;
        FxAllocationObserver observer=fx_platform_account_observer(s,"ui.controls");
        store=memory.allocate(memory.context,(uint32_t)sizeof(*store));
        if(!store)return FX_BUDGET;
        memset(store,0,sizeof(*store));store->memory=memory;store->observer=observer;
        store->used=store->peak=(uint32_t)sizeof(*store);
        fx_allocation_observe(observer,(uint32_t)sizeof(*store),FX_ALLOC_REQUESTED);
        for(uint32_t i=0u;i<FX_UI_CONTROLS;++i)store->inline_order[i]=s->ui_order[i];
    }
    uint32_t directory_capacity=store->directory_capacity,bucket_count=store->bucket_count;
    UiControlBlock **directory=store->directory;uint32_t *buckets=store->buckets;
    UiControlBlock *block=0;FxResult result=FX_BUDGET;
    if(store->blocks==directory_capacity) {
        directory_capacity=directory_capacity?directory_capacity*2u:4u;
        directory=ui_controls_allocate(s,store,directory_capacity*(uint32_t)sizeof(*directory));
        if(!directory)goto failed;
        memset(directory,0,directory_capacity*sizeof(*directory));
        if(store->blocks)memcpy(directory,store->directory,store->blocks*sizeof(*directory));
    }
    uint32_t capacity=FX_UI_CONTROLS+(store->blocks+1u)*UI_CONTROL_CHUNK;
    if(capacity>bucket_count/2u) {
        bucket_count=bucket_count?bucket_count*2u:64u;
        buckets=ui_controls_allocate(s,store,bucket_count*(uint32_t)sizeof(*buckets));
        if(!buckets)goto failed;
        memset(buckets,0,bucket_count*sizeof(*buckets));
    }
    block=ui_controls_allocate(s,store,(uint32_t)sizeof(*block));
    if(!block)goto failed;
    memset(block,0,sizeof(*block));
    if(atomic_load_explicit(&s->phase,memory_order_acquire)!=FX_OWNER_RUNNING){result=FX_STATE;goto failed;}
    /* No fallible operations below. Publish only complete private storage,
     * with ui_lock excluding every graph/snapshot/input reader. */
    if(fresh) {
        s->ui_controls=store;
        fx_allocation_observe(store->observer,(uint32_t)sizeof(*store),FX_ALLOC_PUBLISHED);
    }
    directory[store->blocks]=block;
    fx_allocation_observe(store->observer,(uint32_t)sizeof(*block),FX_ALLOC_PUBLISHED);
    if(directory!=store->directory) {
        if(store->directory)ui_controls_release(store,store->directory,store->directory_capacity*(uint32_t)sizeof(*directory),1u);
        fx_allocation_observe(store->observer,directory_capacity*(uint32_t)sizeof(*directory),FX_ALLOC_PUBLISHED);
        store->directory=directory;store->directory_capacity=directory_capacity;
    }
    ++store->blocks;
    if(buckets!=store->buckets) {
        uint32_t *old=store->buckets,old_count=store->bucket_count;
        store->buckets=buckets;store->bucket_count=bucket_count;
        for(uint32_t i=0u;i<s->control_count;++i)ui_control_insert(s,i);
        fx_allocation_observe(store->observer,bucket_count*(uint32_t)sizeof(*buckets),FX_ALLOC_PUBLISHED);
        if(old)ui_controls_release(store,old,old_count*(uint32_t)sizeof(*old),1u);
    }
    return FX_OK;
failed:
    if(block)ui_controls_release(store,block,(uint32_t)sizeof(*block),0u);
    if(buckets && buckets!=store->buckets)ui_controls_release(store,buckets,bucket_count*(uint32_t)sizeof(*buckets),0u);
    if(directory && directory!=store->directory)ui_controls_release(store,directory,directory_capacity*(uint32_t)sizeof(*directory),0u);
    if(fresh) {
        FxIndexMemory memory=store->memory;FxAllocationObserver observer=store->observer;
        memory.release(memory.context,store);fx_allocation_observe(observer,(uint32_t)sizeof(*store),FX_ALLOC_FREE_PENDING);
    }
    return result;
}
