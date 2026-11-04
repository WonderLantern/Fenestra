#include <assert.h>
#include <stdint.h>
#include "../src/platform/z63_c201/address_space.h"
static _Alignas(4096) uint64_t tables[16][512];
static uintptr_t mapping(void *context,uintptr_t va,uint32_t flags) {
    (void)context;(void)flags;
    if(va>=(uintptr_t)tables && va<(uintptr_t)tables+sizeof(tables))return va;
    if(va==0x40910000u)return UINT64_C(0xa16310000);
    return 0;
}
static uint64_t descriptor(FxZ63AddressSpace *s,uintptr_t va) {
    uint64_t *p=s->tables[0];
    for(unsigned shift=30;shift>12;shift-=9)p=(uint64_t *)(uintptr_t)(p[(va>>shift)&511u]&UINT64_C(0x0000fffffffff000));
    return p[(va>>12)&511u];
}
int main(void) {
    FxZ63AddressSpace s={0};uintptr_t root=0;
    assert(fx_z63_space_init(&s,(void *)(uintptr_t)(UINT64_C(1)<<39),16u,0,mapping)==FX_ARGUMENT);
    assert(fx_z63_space_init(&s,tables,16u,0,mapping)==FX_OK);
    assert(fx_z63_space_map(&s,0x40910000u,4096u,FX_Z63_MAP_READ|FX_Z63_MAP_WRITE|FX_Z63_MAP_EXECUTE)==FX_ARGUMENT);
    assert(fx_z63_space_map(&s,0x40910000u,4096u,FX_Z63_MAP_READ|FX_Z63_MAP_EXECUTE)==FX_OK);
    uint64_t pte=descriptor(&s,0x40910000u);
    assert((pte&UINT64_C(0x0000fffffffff000))==UINT64_C(0xa16310000));
    assert(!(pte&(UINT64_C(1)<<53)) && (pte&(UINT64_C(1)<<54)));
    assert(fx_z63_space_map(&s,0x40911000u,4096u,FX_Z63_MAP_READ)==FX_CAPABILITY);
    assert(fx_z63_space_seal(&s,&root)==FX_OK && root==(uintptr_t)tables);
    assert(fx_z63_space_map(&s,0x40910000u,4096u,FX_Z63_MAP_READ)==FX_STATE);
    return 0;
}
