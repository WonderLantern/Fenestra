#include "fenestra_text_regions.h"

static uintptr_t leaf(FxTextRegion *record){return (uintptr_t)record|1u;}
static const FxTextRegion *record(uintptr_t pointer){return (const FxTextRegion *)(pointer&~(uintptr_t)1u);}
static uintptr_t prefix(uintptr_t key,uint32_t bit){return bit+1u==sizeof(uintptr_t)*8u?0u:key>>(bit+1u);}
static uint32_t side(uintptr_t key,uint32_t bit){return (uint32_t)((key>>bit)&1u);}
static uintptr_t child(const FxTextRegionBranch *b,uint32_t index){return atomic_load_explicit(&b->child[index],memory_order_acquire);}
static const FxTextRegion *last(uintptr_t node,uint32_t *visits){
    while(node && !(node&1u)){++*visits;node=child((const FxTextRegionBranch *)node,1u);}
    if(node)++*visits;
    return node?record(node):0;
}
/* Patricia predecessor: a skipped left subtree is the best lower candidate.
 * Bits strictly decrease, including on the one optional rightmost descent;
 * even monotonic allocation addresses cannot create an unbounded chain. */
static const FxTextRegion *floor_region(const FxTextRegions *index,uintptr_t key,uint32_t *visits){
    uintptr_t node=atomic_load_explicit(&index->root,memory_order_acquire),lower=0u;
    while(node && !(node&1u)){
        const FxTextRegionBranch *b=(const FxTextRegionBranch *)node;++*visits;
        uintptr_t p=prefix(key,b->bit);
        if(p!=b->prefix){if(p>b->prefix)lower=node;node=0u;break;}
        uint32_t bit=side(key,b->bit);
        if(bit)lower=child(b,0u);
        node=child(b,bit);
    }
    if(node){++*visits;const FxTextRegion *r=record(node);if(r->begin<=key)return r;}
    return last(lower,visits);
}
int fx_text_regions_contains(const FxTextRegions *index,uintptr_t address,uintptr_t bytes,uint32_t *steps){
    uint32_t visits=0u;const FxTextRegion *r=index && bytes?floor_region(index,address,&visits):0;
    if(steps)*steps=visits;
    return r && address>=r->begin && address<r->end && bytes<=r->end-address;
}
FxResult fx_text_regions_add(FxTextRegions *index,FxTextRegion *r,const void *base,uintptr_t bytes){
    uintptr_t begin=(uintptr_t)base;
    if(!index || !r || !begin || !bytes || bytes>UINTPTR_MAX-begin || (uintptr_t)r%_Alignof(FxTextRegion))return FX_ARGUMENT;
    uintptr_t domain=atomic_load_explicit(&r->domain,memory_order_acquire);
    if(domain)return domain==(uintptr_t)index && r->begin==begin && r->end==begin+bytes?FX_OK:FX_STALE;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong_explicit(&index->writer,&zero,1u,memory_order_acquire,memory_order_relaxed))return FX_BUSY;
    domain=atomic_load_explicit(&r->domain,memory_order_acquire);
    if(domain){
        FxResult known=domain==(uintptr_t)index && r->begin==begin && r->end==begin+bytes?FX_OK:FX_STALE;
        atomic_store_explicit(&index->writer,0u,memory_order_release);return known;
    }
    uint32_t visits=0u;const FxTextRegion *before=floor_region(index,begin,&visits);
    FxResult result=FX_OK;uintptr_t end=begin+bytes;
    uint32_t covered=(uint32_t)(before && before->end>begin);
    if(covered){
        if(before->end<end)result=FX_INTEGRITY;
        /* A core subobject inside an already retained allocation is covered. */
    } else {
        const FxTextRegion *tail=floor_region(index,end-1u,&visits);
        if(tail && tail->begin>=begin)result=FX_INTEGRITY;
        if(result==FX_OK){
            uintptr_t old=atomic_load_explicit(&index->root,memory_order_acquire);
            r->begin=begin;r->end=end;
            if(!old)atomic_store_explicit(&index->root,leaf(r),memory_order_release);
            else {
                uintptr_t node=old;
                while(!(node&1u)){const FxTextRegionBranch *b=(const FxTextRegionBranch *)node;node=child(b,side(begin,b->bit));}
                uintptr_t difference=begin^record(node)->begin;uint32_t bit=0u;
                while(difference>>=1u)++bit;
                _Atomic uintptr_t *link=&index->root;node=old;
                while(!(node&1u)){
                    FxTextRegionBranch *b=(FxTextRegionBranch *)node;if(b->bit<=bit)break;
                    link=&b->child[side(begin,b->bit)];node=atomic_load_explicit(link,memory_order_acquire);
                }
                FxTextRegionBranch *branch=&r->branch;uint32_t direction=side(begin,bit);
                branch->bit=bit;branch->prefix=prefix(begin,bit);
                atomic_store_explicit(&branch->child[direction],leaf(r),memory_order_relaxed);
                atomic_store_explicit(&branch->child[direction^1u],node,memory_order_relaxed);
                atomic_store_explicit(link,(uintptr_t)branch,memory_order_release);
            }
            atomic_fetch_add_explicit(&index->count,1u,memory_order_relaxed);
        }
    }
    if(result==FX_OK){
        if(covered){r->begin=begin;r->end=end;}
        atomic_store_explicit(&r->domain,(uintptr_t)index,memory_order_release);
    }
    atomic_store_explicit(&index->writer,0u,memory_order_release);return result;
}
FxResult fx_native_text_regions_add(FxTextRegions *index,FxTextRegion *r,const void *base,uintptr_t bytes){
#if defined(__aarch64__) && defined(FX_FREESTANDING)
    uint64_t saved;__asm__ volatile("mrs %0,daif; msr daifset,#3":"=r"(saved)::"memory");
    FxResult result;
    do{result=fx_text_regions_add(index,r,base,bytes);}while(result==FX_BUSY);
    __asm__ volatile("msr daif,%0"::"r"(saved):"memory");return result;
#else
    return fx_text_regions_add(index,r,base,bytes);
#endif
}
