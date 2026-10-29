#include "fenestra_module_bus.h"
#include "fenestra_memory.h"
static int lock(FxModuleBus *b) {
    uint32_t zero=0;return atomic_compare_exchange_strong_explicit(&b->lock,&zero,1u,memory_order_acquire,memory_order_relaxed);
}
static void unlock(FxModuleBus *b){atomic_store_explicit(&b->lock,0u,memory_order_release);}
static void *allocate(FxModuleBus *b,uint32_t bytes,FxAllocationObserver observer) {
    if(b->budget.used>b->budget.limit || bytes>b->budget.limit-b->budget.used)return 0;
    void *p=b->endpoints.memory.allocate(b->endpoints.memory.context,bytes);
    if(p){b->budget.used+=bytes;if(b->budget.used>b->budget.peak)b->budget.peak=b->budget.used;fx_allocation_observe(observer,bytes,FX_ALLOC_REQUESTED);}return p;
}
static void release(FxModuleBus *b,void *p,uint32_t bytes,FxAllocationObserver observer,uint32_t published) {
    b->endpoints.memory.release(b->endpoints.memory.context,p);b->budget.used-=bytes;
    fx_allocation_observe(observer,bytes,published?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);
}
FxResult fx_module_bus_init(FxModuleBus *b,FxIndexMemory memory) {
    if(!b || !memory.allocate || !memory.release)return FX_ARGUMENT;
    if(!lock(b))return FX_BUSY;
    FxResult r=FX_OK;
    if(!b->endpoints.stride) {
        b->budget=(FxSlotBudget){FX_BUS_BUDGET,sizeof(*b),sizeof(*b)};
        r=fx_slot_pool_init(&b->endpoints,memory,&b->budget,sizeof(FxModuleEndpoint));
        if(r==FX_OK){b->buckets=b->inline_buckets;b->bucket_count=16u;b->endpoints.observer=b->metadata;}
    }
    unlock(b);return r;
}
static int running(const FxModuleEndpoint *e){return e && e->owner && e->owner->phase==FX_OWNER_RUNNING;}
static uint32_t hash(const char *name,uint32_t version) {
    uint32_t h=2166136261u^version;
    for(uint32_t i=0;i<64u && name[i];++i)h=(h^(uint8_t)name[i])*16777619u;
    return h^(h>>16);
}
static FxModuleEndpoint *at(FxModuleBus *b,uint32_t id){return fx_slot_pool_at(&b->endpoints,id-1u);}
static FxModuleEndpoint *named(FxModuleBus *b,const char *name,uint32_t version,uint32_t h) {
    for(uint32_t id=b->buckets[h&(b->bucket_count-1u)];id;) {
        FxModuleEndpoint *e=at(b,id);
        if(e->hash==h && e->version==version && !memcmp(e->name,name,64u))return e;
        id=e->next;
    }
    return 0;
}
static void insert(FxModuleBus *b,FxModuleEndpoint *e) {
    uint32_t bucket=e->hash&(b->bucket_count-1u);e->next=b->buckets[bucket];b->buckets[bucket]=(uint32_t)e->token;
}
static void remove_endpoint(FxModuleBus *b,FxModuleEndpoint *e) {
    uint32_t *link=&b->buckets[e->hash&(b->bucket_count-1u)];
    while(*link!=(uint32_t)e->token)link=&at(b,*link)->next;
    *link=e->next;
    if(e->queue){release(b,e->queue,e->capacity*(uint32_t)sizeof(*e->queue),e->queue_observer,1u);--b->allocated_queues;}
    b->queued-=e->count;b->queue_slots-=e->capacity;
    (void)fx_slot_pool_release(&b->endpoints,e->token);
}
static uint32_t reap(FxModuleBus *b) {
    uint32_t removed=0u;
    for(uint32_t i=0;i<b->endpoints.issued;++i) {
        FxModuleEndpoint *e=fx_slot_pool_at(&b->endpoints,i);if(e && !running(e)){remove_endpoint(b,e);++removed;}
    }
    return removed;
}
static FxResult publish(FxModuleBus *b,FxPlatformOwner *o,const FxEndpointRequest *r,uint32_t h,FxServiceReply *a) {
    uint32_t count=b->bucket_count,*buckets=b->buckets;
    if((b->endpoints.used+1u)*2u>count) {
        count*=2u;buckets=allocate(b,count*4u,b->metadata);if(!buckets)return FX_BUDGET;
        memset(buckets,0,count*4u);
    }
    void *storage=0;uint64_t token=0u;
    FxResult result=fx_slot_pool_take(&b->endpoints,&storage,&token);
    if(result!=FX_OK) {
        if(buckets!=b->buckets)release(b,buckets,count*4u,b->metadata,0u);
        return result;
    }
    FxModuleEndpoint *e=storage;
    if(buckets!=b->buckets) {
        uint32_t *old=b->buckets,old_count=b->bucket_count;
        fx_allocation_observe(b->metadata,count*4u,FX_ALLOC_PUBLISHED);b->buckets=buckets;b->bucket_count=count;
        for(uint32_t i=0;i<b->endpoints.issued;++i){FxModuleEndpoint *p=fx_slot_pool_at(&b->endpoints,i);if(p && p!=e)insert(b,p);}
        if(old!=b->inline_buckets)release(b,old,old_count*4u,b->metadata,1u);
    }
    e->owner=o;e->token=token;e->version=r->version;e->hash=h;e->limit=FX_BUS_DEFAULT_QUEUE;
    if(b->owner_queue)e->queue_observer=b->owner_queue(o);
    memcpy(e->name,r->name,64u);insert(b,e);
    a->value[0]=token;a->value[1]=o->owner;a->value[2]=o->generation;return FX_OK;
}
static FxResult resize(FxModuleBus *b,FxModuleEndpoint *e,uint32_t capacity) {
    if(capacity<e->count)return FX_BUSY;
    if(capacity>UINT32_MAX/(uint32_t)sizeof(FxModuleMessage))return FX_BUDGET;
    if(capacity==e->capacity)return FX_OK;
    FxModuleMessage *queue=capacity?allocate(b,capacity*(uint32_t)sizeof(*queue),e->queue_observer):0;
    if(capacity && !queue)return FX_BUDGET;
    for(uint32_t i=0;i<e->count;++i)queue[i]=e->queue[(e->head+i)%e->capacity];
    if(e->queue){release(b,e->queue,e->capacity*(uint32_t)sizeof(*queue),e->queue_observer,1u);--b->allocated_queues;}
    if(queue){fx_allocation_observe(e->queue_observer,capacity*(uint32_t)sizeof(*queue),FX_ALLOC_PUBLISHED);++b->allocated_queues;}
    b->queue_slots=b->queue_slots-e->capacity+capacity;e->queue=queue;e->capacity=capacity;e->head=0u;return FX_OK;
}
static FxResult call(FxModuleBus *b,FxPlatformOwner *o,const FxServiceRequest *q,FxServiceReply *a,FxPlatformOwner **wake) {
    uint64_t op=q->arg[2];
    if(op==FX_BUS_QUERY || op==FX_BUS_ENUM || (op==FX_BUS_CAPACITY && !q->arg[3])) {
        if(q->bytes || (op==FX_BUS_QUERY && q->arg[3]))return FX_ARGUMENT;
        (void)reap(b);
        if(op==FX_BUS_CAPACITY) {
            FxBusCapacity c={.abi=1u,.bytes=sizeof(c),.budget=b->budget.limit,.used=b->budget.used,.peak=b->budget.peak,
                .endpoint_slots=b->endpoints.capacity,.active_endpoints=b->endpoints.used,.allocated_queues=b->allocated_queues,
                .queued_messages=b->queued,.queue_slots=b->queue_slots,.queued_peak=b->queued_peak,.default_queue=FX_BUS_DEFAULT_QUEUE,
                .max_queue=b->budget.limit/(uint32_t)sizeof(FxModuleMessage),.max_payload=FX_BUS_BYTES};
            memcpy(a->data,&c,sizeof(c));a->bytes=sizeof(c);return FX_OK;
        }
        if(op==FX_BUS_QUERY) {
            FxModuleBusInfo info={1u,sizeof(info),0x3feu,0u,0u,FX_BUS_BYTES,b->endpoints.used};
            memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);a->value[0]=info.abi;return FX_OK;
        }
        if(q->arg[3]>b->endpoints.issued)return FX_ARGUMENT;
        for(uint32_t i=(uint32_t)q->arg[3];i<b->endpoints.issued;++i) {
            const FxModuleEndpoint *e=fx_slot_pool_at(&b->endpoints,i);if(!e)continue;
            FxEndpointInfo info={.endpoint=e->token,.version=e->version,.owner=e->owner->owner,.generation=e->owner->generation};
            memcpy(info.name,e->name,sizeof(info.name));memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);a->value[0]=i+1u;return FX_OK;
        }
        return FX_EMPTY;
    }
    if(op==FX_BUS_PUBLISH || op==FX_BUS_LOOKUP) {
        if(q->bytes!=sizeof(FxEndpointRequest) || q->arg[3])return FX_ARGUMENT;
        FxEndpointRequest r;memcpy(&r,q->data,sizeof(r));
        if(!fx_service_name(r.name,64u,1) || !r.version || r.reserved)return FX_ARGUMENT;
        uint32_t h=hash(r.name,r.version);FxModuleEndpoint *e=named(b,r.name,r.version,h);
        if(e && !running(e)){remove_endpoint(b,e);e=0;}
        if(e) {
            if(op==FX_BUS_PUBLISH && e->owner!=o)return FX_BUSY;
            a->value[0]=e->token;a->value[1]=e->owner->owner;a->value[2]=e->owner->generation;return FX_OK;
        }
        if(op==FX_BUS_LOOKUP)return FX_EMPTY;
        FxResult result=publish(b,o,&r,h,a);
        if(result==FX_BUDGET && reap(b))result=publish(b,o,&r,h,a);
        return result;
    }
    if(op==FX_BUS_CAPACITY) {
        if(q->bytes)return FX_ARGUMENT;
        FxModuleEndpoint *e=fx_slot_pool_get(&b->endpoints,q->arg[3]);if(!running(e))return FX_STALE;
        FxBusEndpointCapacity c={1u,sizeof(c),e->limit,e->capacity,e->count,e->peak,e->owner->owner,e->owner->generation,e->token};
        memcpy(a->data,&c,sizeof(c));a->bytes=sizeof(c);return FX_OK;
    }
    if(q->arg[3])return FX_ARGUMENT;
    if(op==FX_BUS_RESERVE) {
        if(q->bytes!=sizeof(FxBusQueueRequest))return FX_ARGUMENT;
        FxBusQueueRequest r;memcpy(&r,q->data,sizeof(r));if(r.reserved)return FX_ARGUMENT;
        FxModuleEndpoint *e=fx_slot_pool_get(&b->endpoints,r.endpoint);if(!running(e))return FX_STALE;
        if(e->owner!=o)return FX_CAPABILITY;
        FxResult result=resize(b,e,r.capacity);
        if(result==FX_OK){e->limit=r.capacity;a->value[0]=e->limit;a->value[1]=e->capacity;a->value[2]=e->count;}return result;
    }
    if(q->bytes!=sizeof(FxModuleMessage))return FX_ARGUMENT;
    FxModuleMessage m;memcpy(&m,q->data,sizeof(m));FxModuleEndpoint *e=fx_slot_pool_get(&b->endpoints,m.endpoint);
    if(!running(e))return FX_STALE;
    if(op==FX_BUS_SEND) {
        if(m.bytes>FX_BUS_BYTES || m.reserved)return FX_BOUNDS;
        if(e->count==e->limit)return FX_BUSY;
        if(e->count==e->capacity) {
            uint32_t capacity=e->capacity?e->capacity*2u:4u;if(capacity>e->limit)capacity=e->limit;
            FxResult result=resize(b,e,capacity);if(result!=FX_OK)return result;
        }
        m.sender=o->owner;m.generation=o->generation;m.reserved=0u;memset(m.data+m.bytes,0,FX_BUS_BYTES-m.bytes);
        e->queue[(e->head+e->count)%e->capacity]=m;++e->count;++b->queued;
        if(e->count>e->peak)e->peak=e->count;
        if(b->queued>b->queued_peak)b->queued_peak=b->queued;
        *wake=e->owner;return FX_OK;
    }
    if(e->owner!=o)return FX_CAPABILITY;
    if(op==FX_BUS_REMOVE){remove_endpoint(b,e);return FX_OK;}
    if(op!=FX_BUS_POLL)return FX_ARGUMENT;
    if(!e->count)return FX_EMPTY;
    memcpy(a->data,&e->queue[e->head],sizeof(FxModuleMessage));a->bytes=sizeof(FxModuleMessage);
    e->head=(e->head+1u)%e->capacity;--e->count;--b->queued;return FX_OK;
}
FxResult fx_module_bus_call(FxModuleBus *b,FxPlatformOwner *o,const FxServiceRequest *q,FxServiceReply *a) {
    if(!b || !o || !q || !a || o->phase!=FX_OWNER_RUNNING)return FX_STATE;
    if(!lock(b))return FX_BUSY;
    FxPlatformOwner *wake=0;FxResult r=b->endpoints.stride?call(b,o,q,a,&wake):FX_STATE;
    unlock(b);if(wake)fx_platform_wake(wake);return r;
}
FxResult fx_module_bus_stop(FxModuleBus *b,FxPlatformOwner *o) {
    if(!b || !o)return FX_ARGUMENT;
    if(!lock(b))return FX_BUSY;
    if(b->endpoints.stride)for(uint32_t i=0;i<b->endpoints.issued;++i) {
        FxModuleEndpoint *e=fx_slot_pool_at(&b->endpoints,i);if(e && e->owner==o)remove_endpoint(b,e);
    }
    unlock(b);return FX_OK;
}
FxResult fx_module_bus_destroy(FxModuleBus *b) {
    if(!b)return FX_ARGUMENT;
    if(!lock(b))return FX_BUSY;
    if(!b->endpoints.stride){unlock(b);return FX_STATE;}
    if(b->endpoints.used){unlock(b);return FX_BUSY;}
    if(b->buckets!=b->inline_buckets)release(b,b->buckets,b->bucket_count*4u,b->metadata,1u);
    FxResult r=fx_slot_pool_destroy(&b->endpoints);
    if(r==FX_OK)memset(b,0,sizeof(*b));else unlock(b);return r;
}
