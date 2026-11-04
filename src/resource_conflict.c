#include "fenestra_resource_conflict.h"
#include "fenestra_memory.h"

static int equal(const char *a,const char *b) {
    for(uint32_t i=0u;i<64u;++i){if(a[i]!=b[i])return 0;if(!a[i])return 1;}return 0;
}
static int enter(FxResourceRegistry *r) {
    uint32_t zero=0u;
    return atomic_compare_exchange_strong_explicit(&r->lock,&zero,1u,memory_order_acquire,memory_order_relaxed);
}
static void leave(FxResourceRegistry *r){atomic_store_explicit(&r->lock,0u,memory_order_release);}
FxResult fx_resource_acquire(FxResourceRegistry *r,FxResourceClaim *claim,uint32_t owner,uint32_t generation,
    const char *name,uint32_t domain,uint64_t address,uint64_t bytes,uint32_t mode,FxResourceConflict *out) {
    if(out)memset(out,0,sizeof(*out));
    if(!r || !claim || !owner || !generation || !name || claim->registry ||
       (mode!=FX_RESOURCE_SHARED && mode!=FX_RESOURCE_EXCLUSIVE) ||
       (domain!=FX_RESOURCE_MEMORY && domain!=FX_RESOURCE_NAMED))return FX_ARGUMENT;
    uint32_t n=0u;while(n<64u && name[n]){if((uint8_t)name[n]<32u || (uint8_t)name[n]>126u)return FX_ARGUMENT;++n;}
    if(!n || n==64u || (domain==FX_RESOURCE_MEMORY && (!bytes || address>UINT64_MAX-bytes)) ||
       (domain==FX_RESOURCE_NAMED && (address || bytes)))return FX_ARGUMENT;
    if(!enter(r))return FX_BUSY;
    for(const FxResourceClaim *p=r->head;p;p=p->next) {
        if(p->owner==owner && p->generation==generation)continue;
        if(p->domain!=domain || (p->mode==FX_RESOURCE_SHARED && mode==FX_RESOURCE_SHARED))continue;
        int overlap=domain==FX_RESOURCE_MEMORY?
            address<p->address+p->bytes && p->address<address+bytes:equal(name,p->name);
        if(!overlap)continue;
        if(out) {
            *out=(FxResourceConflict){.abi=1u,.bytes=sizeof(*out),.requester=owner,.generation=generation,
                .holder=p->owner,.holder_generation=p->generation,.requested_mode=mode,.held_mode=p->mode};
            memcpy(out->requested,name,n+1u);memcpy(out->held,p->name,64u);
        }
        leave(r);return FX_CONFLICT;
    }
    if(r->count==UINT32_MAX){leave(r);return FX_BUDGET;}
    *claim=(FxResourceClaim){.registry=r,.next=r->head,.address=address,.bytes=bytes,
        .owner=owner,.generation=generation,.mode=mode,.domain=domain};
    memcpy(claim->name,name,n+1u);
    if(r->head)r->head->previous=claim;
    r->head=claim;++r->count;leave(r);return FX_OK;
}
FxResult fx_resource_release(FxResourceClaim *claim) {
    return fx_resource_release_many(claim,1u);
}
FxResult fx_resource_release_many(FxResourceClaim *claims,uint32_t count) {
    if(count && !claims)return FX_ARGUMENT;
    FxResourceRegistry *r=0;
    for(uint32_t i=0u;i<count;++i)if(claims[i].registry) {
        if(r && r!=claims[i].registry)return FX_ARGUMENT;
        r=claims[i].registry;
    }
    if(!r)return FX_OK;
    if(!enter(r))return FX_BUSY;
    for(uint32_t i=0u;i<count;++i) {
        FxResourceClaim *claim=&claims[i];if(!claim->registry)continue;
        if(claim->previous)claim->previous->next=claim->next;else r->head=claim->next;
        if(claim->next)claim->next->previous=claim->previous;
        --r->count;memset(claim,0,sizeof(*claim));
    }
    leave(r);return FX_OK;
}
