/* Z8 C3.11 file read cache adapter. Upstream LICENSE/EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#include "fenestra_z8_file_read.h"
#include "fenestra_platform_native.h"
#include "fenestra_machine.h"
#include "fenestra_shared_accounts.h"
#include "fenestra_memory.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#define DENIED UINT32_C(0x80000001)
#define CHUNK 32768u

uint32_t fx_z8_file_read(int32_t fd,void *data,uint32_t bytes,uint32_t *done) {
    if(done)*done=0u;
    if(fd<0 || !data || !done || !bytes)return DENIED;
    FxPlatformIO io=fx_platform_native_io();
    FxMachineInfo info={0};
    if(!io.cache || io.cache(io.context,(uintptr_t)&info,sizeof(info),FX_MACHINE_QUERY)!=FX_OK)return DENIED;
    uint32_t line=info.data_line;
    if(line<4u || line>4096u || (line&(line-1u)))return DENIED;
    uint32_t part=bytes>CHUNK?CHUNK:bytes;
    uint32_t span=(part+line-1u)&~(line-1u),allocation=span+line-1u;
    uint8_t *raw=FN(FX_Z8_ADDR_40d30458,void *(*)(uint32_t,uint32_t))(allocation,0u);
    if(!raw)return DENIED;
    FxAllocationObserver observer=fx_account_observer(
        fx_shared_account_row(fx_native_shared_accounts(),"card.read.scratch"));
    fx_allocation_observe(observer,allocation,FX_ALLOC_REQUESTED);
    uint8_t *buffer=(void *)(((uintptr_t)raw+line-1u)&~((uintptr_t)line-1u));
    uint32_t result=DENIED,native_done=0u;
    /* The entire cache-line span belongs to this allocation. Never invalidate
     * caller scratch, stack neighbors, or controller fields. Before the
     * synchronous native read, discard dirty recycled heap data; afterward
     * preserve any partial-sector bytes copied by the filesystem CPU path. */
    if(io.cache(io.context,(uintptr_t)buffer,span,FX_MACHINE_CLEAN_INVALIDATE)==FX_OK) {
        result=FN(FX_Z8_ADDR_402dcb30,uint32_t (*)(int32_t,void *,uint32_t,uint32_t *))
            (fd,buffer,part,&native_done);
        FxResult synced=io.cache(io.context,(uintptr_t)buffer,span,FX_MACHINE_CLEAN_INVALIDATE);
        if(native_done>part){*done=native_done;result=DENIED;}
        else if(synced!=FX_OK)result=DENIED;
        else {
            *done=native_done;
            if(!result && native_done)memcpy(data,buffer,native_done);
        }
    }
    FN(FX_Z8_ADDR_40d30498,void (*)(void *))(raw);
    fx_allocation_observe(observer,allocation,FX_ALLOC_FREE_PENDING);
    return result;
}
