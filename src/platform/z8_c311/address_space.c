#include "address_space.h"
#include "fenestra_memory.h"
#define PAGE 4096u
#define LIMIT (UINT64_C(1)<<39)
#define ADDRESS UINT64_C(0x0000fffffffff000)
#define UXN (UINT64_C(1)<<54)
#define PXN (UINT64_C(1)<<53)

static uint64_t *leaf(FxZ8AddressSpace *s,uintptr_t va) {
    uint64_t *table=s->tables[0];
    for(uint32_t shift=30u;shift>12u;shift-=9u) {
        uint64_t *entry=&table[(va>>shift)&511u];
        if(!*entry) {
            if(s->used>=s->capacity)return 0;
            *entry=(uintptr_t)s->tables[s->used++]|3u;
        }
        if((*entry&3u)!=3u)return 0;
        uintptr_t next=(uintptr_t)(*entry&ADDRESS),begin=(uintptr_t)s->tables;
        if(next<begin || next>=begin+(uintptr_t)s->used*PAGE || (next&(PAGE-1u)))return 0;
        table=(uint64_t *)next;
    }
    return &table[(va>>12)&511u];
}
FxResult fx_z8_space_init(FxZ8AddressSpace *s,void *tables,uint32_t capacity,
                         void *context,int (*probe)(void *,uintptr_t,uint32_t)) {
    uintptr_t begin=(uintptr_t)tables;
    if(!s || !tables || (begin&(PAGE-1u)) || capacity<3u || capacity>FX_Z8_TABLES ||
       begin>=LIMIT || (uintptr_t)capacity*PAGE>LIMIT-begin || !probe)return FX_ARGUMENT;
    if((uintptr_t)s<begin+(uintptr_t)capacity*PAGE && begin<(uintptr_t)s+sizeof(*s))return FX_ARGUMENT;
    if(s->tables || s->used || s->capacity || s->sealed)return FX_BUSY;
    /* Probing is read-only; no page is cleared before the complete pool passes. */
    for(uint32_t i=0;i<capacity;++i)if(!probe(context,begin+(uintptr_t)i*PAGE,FX_Z8_MAP_READ|FX_Z8_MAP_WRITE))return FX_CAPABILITY;
    memset(tables,0,(size_t)capacity*PAGE);
    *s=(FxZ8AddressSpace){tables,1u,capacity,0u,0u,context,probe};return FX_OK;
}
FxResult fx_z8_space_map(FxZ8AddressSpace *s,uintptr_t begin,uint32_t bytes,uint32_t flags) {
    if(!s || !s->tables || !bytes || (begin&(PAGE-1u)) || (bytes&(PAGE-1u)) ||
       begin>=LIMIT || bytes>LIMIT-begin || !(flags&FX_Z8_MAP_READ) || (flags&~15u) ||
       ((flags&FX_Z8_MAP_WRITE) && (flags&FX_Z8_MAP_EXECUTE)))return FX_ARGUMENT;
    if(s->sealed)return FX_STATE;
    uintptr_t pool=(uintptr_t)s->tables,pool_end=pool+(uintptr_t)s->capacity*PAGE;
    if((flags&(FX_Z8_MAP_USER|FX_Z8_MAP_EXECUTE)) && begin<pool_end && pool<begin+bytes)return FX_CAPABILITY;
    for(uintptr_t va=begin;va<begin+bytes;va+=PAGE)
        if(!s->owned_identity(s->context,va,flags))return FX_CAPABILITY;
    /* MAIR0=0xff in the private gate: normal WB, inner-shareable, AF. */
    uint64_t attributes=UINT64_C(3)|(UINT64_C(3)<<8)|(UINT64_C(1)<<10)|UXN|PXN;
    if(!(flags&FX_Z8_MAP_WRITE))attributes|=UINT64_C(2)<<6;
    if(flags&FX_Z8_MAP_USER)attributes|=UINT64_C(1)<<6;
    if(flags&FX_Z8_MAP_EXECUTE)attributes&=~((flags&FX_Z8_MAP_USER)?UXN:PXN);
    for(uintptr_t va=begin;va<begin+bytes;va+=PAGE) {
        uint64_t *pte=leaf(s,va);
        /* Failure poisons the unpublished view; the caller must discard it.
         * Never silently override an existing permission or keep building. */
        if(!pte || *pte){s->sealed=2u;return pte?FX_INTEGRITY:FX_BUDGET;}
        *pte=va|attributes;
    }
    return FX_OK;
}
FxResult fx_z8_space_seal(FxZ8AddressSpace *s,uintptr_t *root) {
    if(!s || !root || !s->tables)return FX_ARGUMENT;
    if(s->sealed)return FX_STATE;
    s->sealed=1u;*root=(uintptr_t)s->tables;return FX_OK;
}
