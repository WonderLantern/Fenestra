#ifndef FENESTRA_NATIVE_CONTEXT_H
#define FENESTRA_NATIVE_CONTEXT_H
#include <stdint.h>
/* core.context/v1: read one field in the ACTUAL native caller's context.
 * Direct SDK CALL observes its worker; a core.hooks CALL observes the hook.
 * TPIDR is an architectural context pointer, never a portable task/frame ID.
 * Separate reads are not an atomic snapshot and do not grant context ownership. */
enum {
    FX_CONTEXT_TPIDR_EL1,FX_CONTEXT_MPIDR_EL1,FX_CONTEXT_CURRENT_EL,
    FX_CONTEXT_DAIF,FX_CONTEXT_SPSEL,FX_CONTEXT_SP,FX_CONTEXT_COUNTER,
    FX_CONTEXT_FREQUENCY,FX_CONTEXT_TTBR0_EL1,FX_CONTEXT_TTBR1_EL1,
    FX_CONTEXT_SCTLR_EL1,FX_CONTEXT_MAIR_EL1,FX_CONTEXT_TCR_EL1,
    FX_CONTEXT_CPACR_EL1,FX_CONTEXT_FIELDS
};
/* Integer FUNCTION ABI: eight u64 arguments, selector in x0, x1..7 ignored.
 * Unknown selectors return UINT64_MAX. No control-register writes, scheduling,
 * address-space switching or caller-buffer writes; normal AAPCS64 clobbers apply. */
uint64_t fx_native_context(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#endif
