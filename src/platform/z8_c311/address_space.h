#ifndef FX_Z8_C311_ADDRESS_SPACE_H
#define FX_Z8_C311_ADDRESS_SPACE_H
#include "fenestra.h"
#define FX_Z8_TABLES 16u
#define FX_Z8_MAP_READ 1u
#define FX_Z8_MAP_WRITE 2u
#define FX_Z8_MAP_EXECUTE 4u
#define FX_Z8_MAP_USER 8u
/* Owned, identity-backed 39-bit lower VA view. No imported native PTEs and no
 * allocation or publication here. Build before use; never edit active tables.
 * Table RAM must remain EL1-only; each mapped page must pass the platform's
 * original-translation/ownership probe before construction. */
typedef struct {
    uint64_t (*tables)[512];uint32_t used,capacity,sealed,reserved;
    void *context;int (*owned_identity)(void *,uintptr_t,uint32_t);
} FxZ8AddressSpace;
FxResult fx_z8_space_init(FxZ8AddressSpace *,void *tables,uint32_t capacity,
                         void *context,int (*probe)(void *,uintptr_t,uint32_t));
FxResult fx_z8_space_map(FxZ8AddressSpace *,uintptr_t,uint32_t bytes,uint32_t flags);
FxResult fx_z8_space_seal(FxZ8AddressSpace *,uintptr_t *root);
#endif
