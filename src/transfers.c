#include "fenestra_transfers.h"
#include "fenestra_memory.h"
enum {FREE=0u,RESERVED=1u,LIVE=2u,CLAIMED=3u,COMPLETING=4u,RETIRED=7u,MASK=7u};
static void initialize(FxTransferSlot *slots,uint32_t count) {
    for(uint32_t i=0u;i<count;++i){atomic_init(&slots[i].tag,0u);atomic_init(&slots[i].address,0u);atomic_init(&slots[i].bytes,0u);}
}
FxResult fx_transfers_init(FxTransfers *s,FxIndexMemory memory,FxAllocationObserver requests,FxAllocationObserver metadata,uint32_t budget) {
    if(!s || !memory.allocate || !memory.release)return FX_ARGUMENT;
    memset(s,0,sizeof(*s));s->memory=memory;s->requests=requests;s->metadata=metadata;
    s->metadata_limit=budget?budget:FX_TRANSFER_METADATA_BUDGET;
    atomic_init(&s->blocks,0);atomic_init(&s->metadata_reserved,0u);atomic_init(&s->untracked,0u);
    initialize(s->inline_slots,FX_TRANSFER_INLINE);s->configured=1u;return FX_OK;
}
static FxTransferClaim reserve(FxTransferSlot *slots,uint32_t count) {
    for(uint32_t i=0u;i<count;++i){
        uint64_t tag=atomic_load_explicit(&slots[i].tag,memory_order_relaxed);
        if((tag&MASK)==FREE && atomic_compare_exchange_strong_explicit(&slots[i].tag,&tag,tag|RESERVED,memory_order_acquire,memory_order_relaxed))
            return (FxTransferClaim){.slot=&slots[i],.tag=tag|LIVE};
    }
    return (FxTransferClaim){0};
}
static FxTransferClaim storage(FxTransfers *s) {
    FxTransferClaim c=reserve(s->inline_slots,FX_TRANSFER_INLINE);if(c.slot)return c;
    for(FxTransferBlock *b=atomic_load_explicit(&s->blocks,memory_order_acquire);b;b=b->next){
        c=reserve(b->slots,FX_TRANSFER_BLOCK);if(c.slot)return c;
    }
    uint32_t bytes=(uint32_t)sizeof(FxTransferBlock),old=atomic_load_explicit(&s->metadata_reserved,memory_order_relaxed);
    do{if(old>s->metadata_limit || bytes>s->metadata_limit-old)return c;}
    while(!atomic_compare_exchange_weak_explicit(&s->metadata_reserved,&old,old+bytes,memory_order_acq_rel,memory_order_relaxed));
    FxTransferBlock *b=s->memory.allocate(s->memory.context,bytes);
    if(!b){atomic_fetch_sub_explicit(&s->metadata_reserved,bytes,memory_order_relaxed);return c;}
    memset(b,0,sizeof(*b));initialize(b->slots,FX_TRANSFER_BLOCK);
    fx_allocation_observe(s->metadata,bytes,FX_ALLOC_REQUESTED);
    c=reserve(b->slots,FX_TRANSFER_BLOCK);
    FxTransferBlock *head=atomic_load_explicit(&s->blocks,memory_order_relaxed);
    do{b->next=head;}while(!atomic_compare_exchange_weak_explicit(&s->blocks,&head,b,memory_order_release,memory_order_relaxed));
    fx_allocation_observe(s->metadata,bytes,FX_ALLOC_PUBLISHED);return c;
}
FxTransferClaim fx_transfers_begin(FxTransfers *s,uintptr_t address,uint32_t bytes) {
    if(!s || !s->configured || !address || !bytes)return (FxTransferClaim){0};
    fx_allocation_observe(s->requests,bytes,FX_ALLOC_REQUESTED);
    fx_allocation_observe(s->requests,bytes,FX_ALLOC_PUBLISHED);
    FxTransferClaim c=storage(s);c.address=address;c.bytes=bytes;
    if(c.slot){
        atomic_store_explicit(&c.slot->address,address,memory_order_relaxed);
        atomic_store_explicit(&c.slot->bytes,bytes,memory_order_relaxed);
        atomic_store_explicit(&c.slot->tag,c.tag,memory_order_release);
    }else{
        uint64_t old=atomic_load_explicit(&s->untracked,memory_order_relaxed);
        while(old!=UINT64_MAX && !atomic_compare_exchange_weak_explicit(&s->untracked,&old,old+1u,memory_order_relaxed,memory_order_relaxed)){}
        fx_allocation_observe(s->requests,bytes,FX_ALLOC_UNTRACKED);
    }
    return c;
}
static FxTransferClaim claim(FxTransferSlot *slots,uint32_t count,uintptr_t address) {
    for(uint32_t i=0u;i<count;++i){
        uint64_t tag=atomic_load_explicit(&slots[i].tag,memory_order_acquire);
        if((tag&MASK)!=LIVE || atomic_load_explicit(&slots[i].address,memory_order_relaxed)!=address)continue;
        uint64_t desired=(tag&~UINT64_C(7))|CLAIMED;
        if(atomic_compare_exchange_strong_explicit(&slots[i].tag,&tag,desired,memory_order_acq_rel,memory_order_relaxed))
            return (FxTransferClaim){&slots[i],desired,address,atomic_load_explicit(&slots[i].bytes,memory_order_relaxed)};
    }
    return (FxTransferClaim){0};
}
FxTransferClaim fx_transfers_claim(FxTransfers *s,uintptr_t address) {
    if(!s || !s->configured || !address)return (FxTransferClaim){0};
    FxTransferClaim c=claim(s->inline_slots,FX_TRANSFER_INLINE,address);if(c.slot)return c;
    for(FxTransferBlock *b=atomic_load_explicit(&s->blocks,memory_order_acquire);b;b=b->next){
        c=claim(b->slots,FX_TRANSFER_BLOCK,address);if(c.slot)return c;
    }
    return c;
}
void fx_transfers_complete(FxTransfers *s,FxTransferClaim *c) {
    if(!s || !c || !c->slot || !c->bytes)return;
    uint64_t expected=c->tag,done=(expected&~UINT64_C(7))|COMPLETING;
    if((expected&MASK)==CLAIMED && atomic_compare_exchange_strong_explicit(&c->slot->tag,&expected,done,memory_order_acquire,memory_order_relaxed)){
        fx_allocation_observe(s->requests,c->bytes,FX_ALLOC_FREE_PUBLISHED);
        uint64_t next=(done>>3)==(UINT64_MAX>>3)?UINT64_MAX:(done&~UINT64_C(7))+8u;
        atomic_store_explicit(&c->slot->tag,next,memory_order_release);
    }
    memset(c,0,sizeof(*c));
}
void fx_transfers_cancel(FxTransfers *s,FxTransferClaim *c) {
    if(!s || !c || !c->bytes)return;
    if(!c->slot){fx_allocation_observe(s->requests,c->bytes,FX_ALLOC_FREE_PUBLISHED);memset(c,0,sizeof(*c));return;}
    uint64_t expected=c->tag,desired=(expected&~UINT64_C(7))|CLAIMED;
    if((expected&MASK)==LIVE && atomic_compare_exchange_strong_explicit(&c->slot->tag,&expected,desired,memory_order_acq_rel,memory_order_relaxed)){
        c->tag=desired;fx_transfers_complete(s,c);
    }else memset(c,0,sizeof(*c));
}
