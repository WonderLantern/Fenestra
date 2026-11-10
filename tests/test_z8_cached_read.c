#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fenestra_z8_file_read.h"
#include "fenestra_platform_native.h"
#include "fenestra_machine.h"
#include "fenestra_shared_accounts.h"
_Alignas(64) static unsigned char heap[73728];
static unsigned char ddr[73728],dirty[1152],valid[1152],output[40002];
static unsigned char payload[40000];
static unsigned char *raw;
static uint32_t allocated,request_bytes,read_bytes=1768u,native_error,line=64u;
static uint32_t allocations,frees,reads,syncs,accounted,fail_alloc,fail_sync,bad_count;
static unsigned index_of(const void *p){return (unsigned)((const unsigned char *)p-heap);}
void *fixture_allocate(uint32_t n,uint32_t pool) {
    assert(pool==0u && !allocated && n+256u<sizeof(heap));
    ++allocations;if(fail_alloc)return 0;
    memset(heap,0,sizeof(heap));memset(ddr,0x5a,sizeof(ddr));
    memset(dirty,1,sizeof(dirty));memset(valid,1,sizeof(valid));
    raw=heap+131u;allocated=n;return raw;
}
void fixture_release(void *p){assert(p==raw && allocated);allocated=0u;++frees;}
uint32_t fixture_read(int32_t fd,void *p,uint32_t n,uint32_t *done) {
    assert(fd==0 && allocated && n<=32768u);++reads;request_bytes=n;
    unsigned at=index_of(p);assert(at>=index_of(raw) && at+n<=index_of(raw)+allocated);
    uint32_t count=read_bytes<n?read_bytes:n,prefix=count/512u*512u;
    memcpy(ddr+at,payload,prefix); /* DMA bypasses CPU cache. */
    if(prefix<count) {
        unsigned first=(at+prefix)/64u,last=(at+count-1u)/64u;
        for(unsigned i=first;i<=last;++i)if(!valid[i]) {
            memcpy(heap+i*64u,ddr+i*64u,64u);valid[i]=1u;
        }
        memcpy(heap+at+prefix,payload+prefix,count-prefix);
        for(unsigned i=first;i<=last;++i)dirty[i]=1u;
    }
    *done=bad_count?n+1u:count;return native_error;
}
static FxResult cache(void *ctx,uint64_t address,uint32_t bytes,uint32_t operation) {
    (void)ctx;
    if(operation==FX_MACHINE_QUERY) {
        assert(bytes==sizeof(FxMachineInfo));((FxMachineInfo *)(uintptr_t)address)->data_line=line;return FX_OK;
    }
    assert(operation==FX_MACHINE_CLEAN_INVALIDATE);++syncs;
    if(fail_sync==syncs)return FX_IO;
    unsigned at=index_of((void *)(uintptr_t)address);
    assert(!(address&63u) && !(bytes&63u));
    assert(at>=index_of(raw) && at+bytes<=index_of(raw)+allocated);
    for(unsigned i=at/64u;i<(at+bytes)/64u;++i) {
        if(valid[i] && dirty[i])memcpy(ddr+i*64u,heap+i*64u,64u);
        dirty[i]=0u;valid[i]=0u;
        /* Materialize the data that a later CPU load fetches after invalidation. */
        memcpy(heap+i*64u,ddr+i*64u,64u);
    }
    return FX_OK;
}
FxPlatformIO fx_platform_native_io(void){return (FxPlatformIO){.cache=cache};}
FxAccountState *fx_native_shared_accounts(void){return (void *)1;}
FxResult fx_account_get(FxAccountState *s,const char *n,uint32_t f,uint64_t b,FxAccountRow **out) {
    assert(s && !strcmp(n,"card.read.scratch") && f==(FX_ACCOUNT_MANAGED|FX_ACCOUNT_SHARED) && !b);
    *out=(void *)1;return FX_OK;
}
static void event(void *ctx,uint32_t bytes,uint32_t operation) {
    assert(ctx && bytes);
    if(operation==FX_ALLOC_REQUESTED){assert(!accounted);accounted=bytes;}
    else {assert(operation==FX_ALLOC_FREE_PENDING && accounted==bytes);accounted=0u;}
}
FxAllocationObserver fx_account_observer(FxAccountRow *r){return (FxAllocationObserver){r,event};}
static void reset(void) {
    assert(!allocated && !accounted);
    allocations=frees=reads=syncs=0u;fail_alloc=fail_sync=bad_count=native_error=0u;
    line=64u;read_bytes=1768u;memset(output,0xa5,sizeof(output));
}
int main(void) {
    for(unsigned i=0;i<sizeof(payload);++i)payload[i]=(unsigned char)(i*37u+11u);
    /* A dirty, zeroed direct destination loses DMA bytes on writeback.
     * This checks that the noncoherent fixture detects the old failure. */
    reset();unsigned char *p=fixture_allocate(2000u,0u);
    p=(void *)(((uintptr_t)p+63u)&~(uintptr_t)63u);
    uint32_t done=0;assert(!fixture_read(0,p,1769u,&done));
    assert(cache(0,(uintptr_t)p,1792u,FX_MACHINE_CLEAN_INVALIDATE)==FX_OK);
    assert(memcmp(p,payload,1768u));fixture_release(raw);

    reset();assert(!fx_z8_file_read(0,output+1u,1769u,&done));
    assert(done==1768u && !memcmp(output+1u,payload,done));
    assert(output[0]==0xa5 && output[1769]==0xa5 && output[1770]==0xa5);
    assert(syncs==2u && allocations==1u && frees==1u && !accounted);

    reset();read_bytes=0u;assert(!fx_z8_file_read(0,output+1u,1769u,&done));
    assert(!done && output[1]==0xa5 && frees==1u);
    reset();read_bytes=40000u;assert(!fx_z8_file_read(0,output+1u,40000u,&done));
    assert(done==32768u && request_bytes==32768u && !memcmp(output+1u,payload,done) && output[32769]==0xa5);
    reset();native_error=99u;assert(fx_z8_file_read(0,output+1u,1769u,&done)==99u);
    assert(done==1768u && output[1]==0xa5 && frees==1u);
    reset();bad_count=1u;assert(fx_z8_file_read(0,output+1u,1769u,&done));
    assert(done==1770u && output[1]==0xa5 && frees==1u);
    reset();fail_alloc=1u;assert(fx_z8_file_read(0,output+1u,1769u,&done));
    assert(!done && !reads && !syncs && !frees && !accounted);
    for(unsigned failure=1u;failure<=2u;++failure) {
        reset();fail_sync=failure;assert(fx_z8_file_read(0,output+1u,1769u,&done));
        assert(!done && output[1]==0xa5 && frees==1u && reads==failure-1u && !accounted);
    }
    reset();line=48u;assert(fx_z8_file_read(0,output,1u,&done));
    assert(!done && !allocations);
    reset();assert(fx_z8_file_read(-1,output,1u,&done) && !done && !allocations);
    assert(fx_z8_file_read(0,output,0u,&done) && !allocations);
    puts("PASS: dirty-cache DMA regression, CPU tail, bounded reads, canaries, errors and cleanup");
}
