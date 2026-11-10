#if !defined(FX_ZF_EXPERIMENTAL_ADDRESS_SPACE) || FX_ZF_EXPERIMENTAL_ADDRESS_SPACE != 1
#error "Zf private address space is experimental"
#endif
#include "address_space.h"
#include "fenestra_memory.h"

#define PAGE 4096u
#define LIMIT (UINT64_C(1)<<39)
#define ADDRESS UINT64_C(0x0000fffffffff000)
#define UXN (UINT64_C(1)<<54)
#define PXN (UINT64_C(1)<<53)

static uint64_t *leaf(FxZfAddressSpace *space,uintptr_t va) {
    uint64_t *table=space->tables[0];
    for(uint32_t shift=30u;shift>12u;shift-=9u) {
        uint64_t *entry=&table[(va>>shift)&511u];
        if(!*entry) {
            if(space->used>=space->capacity)return 0;
            *entry=(uintptr_t)space->tables[space->used++]|3u;
        }
        if((*entry&3u)!=3u)return 0;
        uintptr_t next=(uintptr_t)(*entry&ADDRESS);
        uintptr_t begin=(uintptr_t)space->tables;
        if(next<begin || next>=begin+(uintptr_t)space->used*PAGE || (next&(PAGE-1u)))return 0;
        table=(uint64_t *)next;
    }
    return &table[(va>>12)&511u];
}

FxResult fx_zf_space_init(FxZfAddressSpace *space,void *tables,uint32_t capacity,
                          void *context,int (*probe)(void *,uintptr_t,uint32_t)) {
    uintptr_t begin=(uintptr_t)tables;
    if(!space || !tables || (begin&(PAGE-1u)) || capacity<3u || capacity>FX_ZF_TABLES ||
       begin>=LIMIT || (uintptr_t)capacity*PAGE>LIMIT-begin || !probe)return FX_ARGUMENT;
    if((uintptr_t)space<begin+(uintptr_t)capacity*PAGE &&
       begin<(uintptr_t)space+sizeof(*space))return FX_ARGUMENT;
    if(space->tables || space->used || space->capacity || space->sealed)return FX_BUSY;
    for(uint32_t i=0;i<capacity;++i)
        if(!probe(context,begin+(uintptr_t)i*PAGE,FX_ZF_MAP_READ|FX_ZF_MAP_WRITE))return FX_CAPABILITY;
    memset(tables,0,(size_t)capacity*PAGE);
    *space=(FxZfAddressSpace){tables,1u,capacity,0u,0u,context,probe};
    return FX_OK;
}

FxResult fx_zf_space_map(FxZfAddressSpace *space,uintptr_t begin,uint32_t bytes,uint32_t flags) {
    if(!space || !space->tables || !bytes || (begin&(PAGE-1u)) || (bytes&(PAGE-1u)) ||
       begin>=LIMIT || bytes>LIMIT-begin || !(flags&FX_ZF_MAP_READ) || (flags&~15u) ||
       ((flags&FX_ZF_MAP_WRITE) && (flags&FX_ZF_MAP_EXECUTE)))return FX_ARGUMENT;
    if(space->sealed)return FX_STATE;
    uintptr_t pool=(uintptr_t)space->tables,pool_end=pool+(uintptr_t)space->capacity*PAGE;
    if((flags&(FX_ZF_MAP_USER|FX_ZF_MAP_EXECUTE)) && begin<pool_end && pool<begin+bytes)
        return FX_CAPABILITY;
    for(uintptr_t va=begin;va<begin+bytes;va+=PAGE)
        if(!space->owned_identity(space->context,va,flags))return FX_CAPABILITY;
    uint64_t attributes=UINT64_C(3)|(UINT64_C(3)<<8)|(UINT64_C(1)<<10)|UXN|PXN;
    if(!(flags&FX_ZF_MAP_WRITE))attributes|=UINT64_C(2)<<6;
    if(flags&FX_ZF_MAP_USER)attributes|=UINT64_C(1)<<6;
    if(flags&FX_ZF_MAP_EXECUTE)attributes&=~((flags&FX_ZF_MAP_USER)?UXN:PXN);
    for(uintptr_t va=begin;va<begin+bytes;va+=PAGE) {
        uint64_t *pte=leaf(space,va);
        if(!pte || *pte){space->sealed=2u;return pte?FX_INTEGRITY:FX_BUDGET;}
        *pte=va|attributes;
    }
    return FX_OK;
}

FxResult fx_zf_space_seal(FxZfAddressSpace *space,uintptr_t *root) {
    if(!space || !root || !space->tables)return FX_ARGUMENT;
    if(space->sealed)return FX_STATE;
    space->sealed=1u;*root=(uintptr_t)space->tables;return FX_OK;
}
