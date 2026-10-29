#ifndef FENESTRA_NATIVE_MAPPING_H
#define FENESTRA_NATIVE_MAPPING_H
#include <stdatomic.h>
#include <stdint.h>
struct FxNativeMappingLease;
typedef struct { _Atomic uint32_t lock;struct FxNativeMappingLease *claims;} FxNativeMappingRegistry;
/* Owned by the existing native menu heap state, zeroed with that state.
 * Never writable static data in the firmware's executable padding. */
FxNativeMappingRegistry *fx_native_mappings(void);
#endif
