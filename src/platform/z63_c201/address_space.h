#ifndef FX_Z63_C201_ADDRESS_SPACE_H
#define FX_Z63_C201_ADDRESS_SPACE_H
#include "fenestra.h"
#define FX_Z63_TABLES 16u
#define FX_Z63_MAP_READ 1u
#define FX_Z63_MAP_WRITE 2u
#define FX_Z63_MAP_EXECUTE 4u
#define FX_Z63_MAP_USER 8u
/* Owned, translated 39-bit lower VA view. No imported native PTEs and no
 * allocation or publication here. Build before use; never edit active tables.
 * Table RAM must remain EL1-only; each mapped page must pass the platform's
 * original-translation/ownership probe (physical page, zero on failure) before construction. */
typedef struct {
    uint64_t (*tables)[512];uint32_t used,capacity,sealed,reserved;
    void *context;uintptr_t (*owned_mapping)(void *,uintptr_t,uint32_t);
} FxZ63AddressSpace;
FxResult fx_z63_space_init(FxZ63AddressSpace *,void *tables,uint32_t capacity,
                         void *context,uintptr_t (*probe)(void *,uintptr_t,uint32_t));
FxResult fx_z63_space_map(FxZ63AddressSpace *,uintptr_t,uint32_t bytes,uint32_t flags);
FxResult fx_z63_space_seal(FxZ63AddressSpace *,uintptr_t *root);
#endif
