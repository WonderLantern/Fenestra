#ifndef FX_ZF_C301_ADDRESS_SPACE_H
#define FX_ZF_C301_ADDRESS_SPACE_H
#include "fenestra.h"

#define FX_ZF_TABLES 16u
#define FX_ZF_MAP_READ 1u
#define FX_ZF_MAP_WRITE 2u
#define FX_ZF_MAP_EXECUTE 4u
#define FX_ZF_MAP_USER 8u

/* Unpublished Zf-owned lower-VA view. Every mapped identity page is checked
 * through the owner-supplied probe before a descriptor is written. The table
 * pool is private EL1 memory and is never imported from Nikon's active tables. */
typedef struct {
    uint64_t (*tables)[512];
    uint32_t used,capacity,sealed,reserved;
    void *context;
    int (*owned_identity)(void *,uintptr_t,uint32_t);
} FxZfAddressSpace;

FxResult fx_zf_space_init(FxZfAddressSpace *,void *tables,uint32_t capacity,
                          void *context,int (*probe)(void *,uintptr_t,uint32_t));
FxResult fx_zf_space_map(FxZfAddressSpace *,uintptr_t,uint32_t bytes,uint32_t flags);
FxResult fx_zf_space_seal(FxZfAddressSpace *,uintptr_t *root);
#endif
