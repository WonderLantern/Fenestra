/* Owned AArch64 module SIMD state. Shared offsets are asserted by both workers. */
#ifndef FENESTRA_SIMD_H
#define FENESTRA_SIMD_H
#define FX_MODULE_SIMD_ENABLE_OFFSET 560
#define FX_MODULE_SIMD_OFFSET 576
#define FX_SIMD_STATE_BYTES 528
#define FX_PRIVATE_FRAME_BYTES 720
#ifdef __ASSEMBLER__
.macro fx_simd_save base, offset
    stp q0,q1,[\base,#(\offset+0)]
    stp q2,q3,[\base,#(\offset+32)]
    stp q4,q5,[\base,#(\offset+64)]
    stp q6,q7,[\base,#(\offset+96)]
    stp q8,q9,[\base,#(\offset+128)]
    stp q10,q11,[\base,#(\offset+160)]
    stp q12,q13,[\base,#(\offset+192)]
    stp q14,q15,[\base,#(\offset+224)]
    stp q16,q17,[\base,#(\offset+256)]
    stp q18,q19,[\base,#(\offset+288)]
    stp q20,q21,[\base,#(\offset+320)]
    stp q22,q23,[\base,#(\offset+352)]
    stp q24,q25,[\base,#(\offset+384)]
    stp q26,q27,[\base,#(\offset+416)]
    stp q28,q29,[\base,#(\offset+448)]
    stp q30,q31,[\base,#(\offset+480)]
    mrs x8,fpcr
    mrs x9,fpsr
    str x8,[\base,#(\offset+512)]
    str x9,[\base,#(\offset+520)]
.endm
.macro fx_simd_restore base, offset
    ldp q0,q1,[\base,#(\offset+0)]
    ldp q2,q3,[\base,#(\offset+32)]
    ldp q4,q5,[\base,#(\offset+64)]
    ldp q6,q7,[\base,#(\offset+96)]
    ldp q8,q9,[\base,#(\offset+128)]
    ldp q10,q11,[\base,#(\offset+160)]
    ldp q12,q13,[\base,#(\offset+192)]
    ldp q14,q15,[\base,#(\offset+224)]
    ldp q16,q17,[\base,#(\offset+256)]
    ldp q18,q19,[\base,#(\offset+288)]
    ldp q20,q21,[\base,#(\offset+320)]
    ldp q22,q23,[\base,#(\offset+352)]
    ldp q24,q25,[\base,#(\offset+384)]
    ldp q26,q27,[\base,#(\offset+416)]
    ldp q28,q29,[\base,#(\offset+448)]
    ldp q30,q31,[\base,#(\offset+480)]
    ldr x8,[\base,#(\offset+512)]
    ldr x9,[\base,#(\offset+520)]
    msr fpcr,x8
    msr fpsr,x9
.endm
.macro fx_simd_clear
    movi v0.16b,#0
    movi v1.16b,#0
    movi v2.16b,#0
    movi v3.16b,#0
    movi v4.16b,#0
    movi v5.16b,#0
    movi v6.16b,#0
    movi v7.16b,#0
    movi v8.16b,#0
    movi v9.16b,#0
    movi v10.16b,#0
    movi v11.16b,#0
    movi v12.16b,#0
    movi v13.16b,#0
    movi v14.16b,#0
    movi v15.16b,#0
    movi v16.16b,#0
    movi v17.16b,#0
    movi v18.16b,#0
    movi v19.16b,#0
    movi v20.16b,#0
    movi v21.16b,#0
    movi v22.16b,#0
    movi v23.16b,#0
    movi v24.16b,#0
    movi v25.16b,#0
    movi v26.16b,#0
    movi v27.16b,#0
    movi v28.16b,#0
    movi v29.16b,#0
    movi v30.16b,#0
    movi v31.16b,#0
    msr fpcr,xzr
    msr fpsr,xzr
.endm
#else
#include <stdint.h>
typedef struct { uint64_t vector[64],fpcr,fpsr; } FxSimdState;
_Static_assert(sizeof(FxSimdState)==FX_SIMD_STATE_BYTES,"AArch64 Q/FPCR/FPSR context");
#endif
#endif
