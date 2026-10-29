#ifndef FENESTRA_MMU_SDK_H
#define FENESTRA_MMU_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_mmu_context.h"

/* Private, initially empty tables in one exported, zero-filled core.memory
 * allocation. This describes storage; it neither changes TTBR nor claims
 * physical contiguity, DMA ownership or exclusion of other native users. */
typedef struct {uint64_t context,root,pool;uint32_t pages,used_bytes;} FxMmuArenaLayout;
static inline FxResult fx_mmu_arena_layout(uint64_t address,uint32_t capacity,
    uint32_t pages,FxMmuArenaLayout *out) {
    const uint64_t limit=UINT64_C(0x8000000000);
    if(!out || !address || (address&7u) || !pages || pages>160u)return FX_ARGUMENT;
    if(address>=limit || capacity>limit-address)return FX_BOUNDS;
    uint64_t root=(address+sizeof(FxNativeMmuContext)+4095u)&~UINT64_C(4095);
    uint64_t end=root+4096u+(uint64_t)pages*4096u;
    if(end>address+capacity)return FX_BOUNDS;
    *out=(FxMmuArenaLayout){address,root,root+4096u,pages,(uint32_t)(end-address)};
    return FX_OK;
}

/* Verify every pool page's PA before supplying physical_pool: a virtually
 * contiguous heap allocation need not be physically contiguous. The root
 * can have a different VA->PA offset. Do not copy the stock bitmap/context.
 * Backing root/pool pages must still be zero and unpublished when copied. */
static inline FxResult fx_mmu_context_prepare(FxNativeMmuContext *out,
    const FxMmuArenaLayout *layout,uint64_t physical_root,uint64_t physical_pool) {
    if(!out || !layout)return FX_ARGUMENT;
    FxMmuArenaLayout actual;
    FxResult r=fx_mmu_arena_layout(layout->context,layout->used_bytes,layout->pages,&actual);
    if(r!=FX_OK)return r;
    if(actual.root!=layout->root || actual.pool!=layout->pool || actual.used_bytes!=layout->used_bytes)return FX_ARGUMENT;
    const uint64_t limit=UINT64_C(0x1000000000),bytes=(uint64_t)layout->pages*4096u;
    if((physical_root|physical_pool)&4095u)return FX_ARGUMENT;
    if(physical_root>limit-4096u || physical_pool>limit-bytes)return FX_BOUNDS;
    if(physical_root<physical_pool+bytes && physical_pool<physical_root+4096u)return FX_ARGUMENT;
    *out=(FxNativeMmuContext){.root=(uintptr_t)layout->root,.pool=(uintptr_t)layout->pool,
        .pages=layout->pages,.physical_root=physical_root,.physical_pool=physical_pool};
    return FX_OK;
}

/* Copy the descriptor to the arena's first bytes. No kernel pointer is
 * dereferenced by EL0. Finish this request before opening native users. */
static inline FxResult fx_sdk_mmu_context_write(FxSdkClient *c,const FxServiceModule *a,
    uint64_t memory,uint64_t buffer,const FxNativeMmuContext *context) {
    struct {FxMemoryRequest request;FxNativeMmuContext context;} payload={
        .request={.handle=buffer,.bytes=sizeof(FxNativeMmuContext)}};
    if(!context)return FX_ARGUMENT;
    payload.context=*context;
    return fx_sdk_platform(c,a,FX_BIND_CALL,memory,FX_MEMORY_WRITE,0u,&payload,sizeof(payload));
}
/* Open the matching rtos.mmu.context.* binding. FX_OK is request submission;
 * after the worker returns, reply.value[0] is the signed native int32 result.
 * All writers of this context/pool must use one original task/mutex protocol.
 * Calls do not publish tables to a CPU, flush aliases, or coordinate DMA. */
static inline FxResult fx_sdk_mmu_map(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint64_t context,uint64_t address,uint64_t bytes,uint64_t physical,uint32_t attributes) {
    uint64_t args[8]={context,address,bytes,physical,attributes&0x9fffffffu,0u,UINT64_MAX};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));
}
static inline FxResult fx_sdk_mmu_protect(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint64_t context,uint64_t address,uint64_t bytes,uint32_t attributes) {
    attributes&=0x9fffffffu;
    if(attributes&0x20u){if(attributes&4u)attributes&=~0x20u;else if(attributes&8u)attributes&=~8u;}
    uint64_t args[8]={context,address,bytes,attributes,0u,UINT64_MAX};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));
}
static inline FxResult fx_sdk_mmu_unmap(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint64_t context,uint64_t address,uint64_t bytes) {
    uint64_t args[8]={context,address,bytes,0u,UINT64_MAX};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));
}
/* translate/attributes write through output_address in an exported native
 * buffer (8/4 bytes). Read it only after a successful native reply. */
static inline FxResult fx_sdk_mmu_query(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint64_t context,uint64_t address,uint64_t output_address) {
    uint64_t args[8]={context,address,output_address,UINT64_MAX};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,0u,0u,args,sizeof(args));
}
#endif
