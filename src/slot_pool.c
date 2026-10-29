#include "fenestra_slot_pool.h"
#include "fenestra_memory.h"
typedef struct {uint32_t generation,next,used,retained;} Slot;
static void *allocate(FxSlotPool *p,uint32_t bytes) {
    FxSlotBudget *b=p->budget;
    if(b->used>b->limit || bytes>b->limit-b->used)return 0;
    void *memory=p->memory.allocate(p->memory.context,bytes);if(!memory)return 0;
    b->used+=bytes;if(b->used>b->peak)b->peak=b->used;
    fx_allocation_observe(p->observer,bytes,FX_ALLOC_REQUESTED);return memory;
}
static void release(FxSlotPool *p,void *memory,uint32_t bytes,uint32_t published) {
    p->memory.release(p->memory.context,memory);p->budget->used-=bytes;
    fx_allocation_observe(p->observer,bytes,published?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);
}
static Slot *slot(const FxSlotPool *p,uint32_t index) {
    return (Slot *)((uint8_t *)p->chunks[index/FX_SLOT_POOL_CHUNK]+(index%FX_SLOT_POOL_CHUNK)*p->stride);
}
static Slot *lookup(const FxSlotPool *p,uint64_t token) {
    uint32_t id=(uint32_t)token,generation=(uint32_t)(token>>32);
    if(!p || !p->stride || !id || id>p->issued || !generation)return 0;
    Slot *s=slot(p,id-1u);return s->used && s->generation==generation?s:0;
}
FxResult fx_slot_pool_init(FxSlotPool *p,FxIndexMemory memory,FxSlotBudget *budget,uint32_t bytes) {
    if(!p || !memory.allocate || !memory.release || !budget || !budget->limit || !bytes || bytes>65536u)return FX_ARGUMENT;
    if(p->stride || p->chunks)return FX_BUSY;
    *p=(FxSlotPool){.memory=memory,.budget=budget,.stride=(uint32_t)((sizeof(Slot)+bytes+7u)&~(size_t)7u),.item_bytes=bytes};
    return FX_OK;
}
static FxResult grow(FxSlotPool *p) {
    if(p->capacity==FX_SLOT_POOL_LIMIT)return FX_BUDGET;
    uint32_t count=FX_SLOT_POOL_LIMIT-p->capacity;if(count>FX_SLOT_POOL_CHUNK)count=FX_SLOT_POOL_CHUNK;
    uint32_t index_capacity=p->index_capacity;
    void **index=p->chunks;
    if(p->chunk_count==index_capacity) {
        index_capacity=index_capacity?index_capacity*2u:4u;
        index=allocate(p,index_capacity*(uint32_t)sizeof(*index));if(!index)return FX_BUDGET;
        memset(index,0,index_capacity*sizeof(*index));
        if(p->chunk_count)memcpy(index,p->chunks,p->chunk_count*sizeof(*index));
    }
    void *chunk=allocate(p,count*p->stride);
    if(!chunk) {
        if(index!=p->chunks)release(p,index,index_capacity*(uint32_t)sizeof(*index),0u);
        return FX_BUDGET;
    }
    memset(chunk,0,count*p->stride);fx_allocation_observe(p->observer,count*p->stride,FX_ALLOC_PUBLISHED);index[p->chunk_count]=chunk;
    if(index!=p->chunks) {
        if(p->chunks)release(p,p->chunks,p->index_capacity*(uint32_t)sizeof(*index),1u);
        fx_allocation_observe(p->observer,index_capacity*(uint32_t)sizeof(*index),FX_ALLOC_PUBLISHED);
        p->chunks=index;p->index_capacity=index_capacity;
    }
    ++p->chunk_count;p->capacity+=count;return FX_OK;
}
FxResult fx_slot_pool_take(FxSlotPool *p,void **out,uint64_t *token) {
    if(!p || !p->stride || !out || *out || !token)return FX_ARGUMENT;
    uint32_t index;Slot *s;
    if(p->free_head) {
        index=p->free_head-1u;s=slot(p,index);p->free_head=s->next;
    } else {
        if(p->issued==p->capacity){FxResult r=grow(p);if(r!=FX_OK)return r;}
        index=p->issued++;s=slot(p,index);
    }
    ++s->generation;s->next=0u;s->used=1u;s->retained=0u;++p->used;
    *out=s+1;*token=((uint64_t)s->generation<<32)|(index+1u);return FX_OK;
}
void *fx_slot_pool_get(const FxSlotPool *p,uint64_t token) {Slot *s=lookup(p,token);return s?s+1:0;}
void *fx_slot_pool_at(const FxSlotPool *p,uint32_t index) {
    if(!p || index>=p->issued)return 0;
    Slot *s=slot(p,index);return s->used?s+1:0;
}
FxResult fx_slot_pool_retain(FxSlotPool *p,uint64_t token) {
    Slot *s=lookup(p,token);if(!s)return FX_STALE;
    if(!s->retained){s->retained=1u;++p->retained;}return FX_OK;
}
FxResult fx_slot_pool_release(FxSlotPool *p,uint64_t token) {
    Slot *s=lookup(p,token);if(!s)return FX_STALE;
    if(s->retained)--p->retained;
    --p->used;s->used=s->retained=0u;
    memset(s+1,0,p->item_bytes);
    /* Exhausted generations are retired permanently, never wrapped to ABA. */
    if(s->generation!=UINT32_MAX){s->next=p->free_head;p->free_head=(uint32_t)token;}
    return FX_OK;
}
uint32_t fx_slot_pool_limit(const FxSlotPool *p) {
    if(!p || !p->stride)return 0u;
    uint32_t count=(p->budget->limit/(FX_SLOT_POOL_CHUNK*p->stride+(uint32_t)sizeof(void *)))*FX_SLOT_POOL_CHUNK;
    return count>FX_SLOT_POOL_LIMIT?FX_SLOT_POOL_LIMIT:count;
}
FxResult fx_slot_pool_destroy(FxSlotPool *p) {
    if(!p || !p->stride)return FX_ARGUMENT;
    if(p->used)return FX_BUSY;
    for(uint32_t i=0u;i<p->chunk_count;++i) {
        uint32_t n=FX_SLOT_POOL_LIMIT-i*FX_SLOT_POOL_CHUNK;if(n>FX_SLOT_POOL_CHUNK)n=FX_SLOT_POOL_CHUNK;
        release(p,p->chunks[i],n*p->stride,1u);
    }
    if(p->chunks)release(p,p->chunks,p->index_capacity*(uint32_t)sizeof(void *),1u);
    memset(p,0,sizeof(*p));return FX_OK;
}
