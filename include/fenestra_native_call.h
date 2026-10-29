#ifndef FENESTRA_NATIVE_CALL_H
#define FENESTRA_NATIVE_CALL_H
#include "fenestra.h"
/* AAPCS64 caller frame. Pointers reference retained native objects, not EL0
 * exchange/stack addresses. The caller supplies the actual signature/layout.
 * x8 is the indirect-result address. Results include x0/x1 and optional v0..3.
 * SIMD requires the original task's FP/SIMD context contract; core never
 * changes CPACR or invents that task contract. No variadic signature inference. */
#define FX_NATIVE_STACK_BYTES 256u
enum {FX_NATIVE_CALL_SIMD=1u};
typedef struct {
    uint32_t abi,bytes,flags,stack_bytes;
    uint64_t integer[9],reserved;
    _Alignas(16) uint8_t vector[8][16];
    uint8_t stack[FX_NATIVE_STACK_BYTES];
    uint64_t result[2];
    uint8_t vector_result[4][16];
} FxNativeCallFrame;
FxResult fx_native_call_frame_check(const FxNativeCallFrame *);
void fx_native_aapcs64(FxNativeCallFrame *,uintptr_t function);
#endif
