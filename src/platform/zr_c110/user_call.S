/* EL0 component gate. Kernel stack, call frames, tables and Loader state
 * remain EL1-only. Normal return uses a dedicated read-only SVC page. */
#include "fenestra_simd.h"
.text
.global fx_c110_user_call
.type fx_c110_user_call,%function
fx_c110_user_call:
    stp x19,x20,[sp,#-128]!
    stp x21,x22,[sp,#16]
    stp x23,x24,[sp,#32]
    stp x25,x26,[sp,#48]
    stp x27,x28,[sp,#64]
    stp x29,x30,[sp,#80]
    mrs x8,sp_el0
    mrs x9,ttbr0_el1
    stp x8,x9,[sp,#96]
    str x4,[sp,#112]
    mov x8,sp
    str x8,[x4,#240] /* FxC110Demo.user_anchor, protected from EL0 */
    ldr x9,[x4,#544] /* continuation.pending */
    cbnz x9,3f
    ldr w8,[x4,#264]
    add w8,w8,#1
    str w8,[x4,#264]
    msr elr_el1,x0
    msr sp_el0,x3
3:
    msr ttbr0_el1,x2
    dsb ish
    tlbi vmalle1
    dsb ish
    isb
    ldr x9,[x4,#544]
    cbnz x9,4f
    fx_simd_clear /* Fresh invocation cannot observe Nikon/another owner Q state. */
    mov x8,#0x340 /* EL0t, IRQ enabled; FIQ/SError/debug stay masked. */
    msr spsr_el1,x8
    mov x0,x1
    mov x1,xzr
    mov x2,xzr
    mov x3,xzr
    mov x4,xzr
    mov x5,xzr
    mov x6,xzr
    mov x7,xzr
    mov x8,xzr
    mov x9,xzr
    mov x10,xzr
    mov x11,xzr
    mov x12,xzr
    mov x13,xzr
    mov x14,xzr
    mov x15,xzr
    mov x16,xzr
    mov x17,xzr
    mov x18,xzr
    mov x19,xzr
    mov x20,xzr
    mov x21,xzr
    mov x22,xzr
    mov x23,xzr
    mov x24,xzr
    mov x25,xzr
    mov x26,xzr
    mov x27,xzr
    mov x28,xzr
    mov x29,xzr
    adrp x30,fx_c110_user_return
    add x30,x30,:lo12:fx_c110_user_return
    eret
4:  add x30,x4,#272
    fx_simd_restore x30,(FX_MODULE_SIMD_OFFSET-272)
    ldr x8,[x30,#248]
    msr sp_el0,x8
    ldr x8,[x30,#256]
    msr elr_el1,x8
    ldr x8,[x30,#264]
    msr spsr_el1,x8
    ldp x0,x1,[x30]
    ldp x2,x3,[x30,#16]
    ldp x4,x5,[x30,#32]
    ldp x6,x7,[x30,#48]
    ldp x8,x9,[x30,#64]
    ldp x10,x11,[x30,#80]
    ldp x12,x13,[x30,#96]
    ldp x14,x15,[x30,#112]
    ldp x16,x17,[x30,#128]
    ldp x18,x19,[x30,#144]
    ldp x20,x21,[x30,#160]
    ldp x22,x23,[x30,#176]
    ldp x24,x25,[x30,#192]
    ldp x26,x27,[x30,#208]
    ldp x28,x29,[x30,#224]
    ldr x30,[x30,#240]
    eret
.global fx_c110_user_resume
fx_c110_user_resume:
    ldp x8,x9,[sp,#96]
    msr sp_el0,x8
    msr ttbr0_el1,x9
    dsb ish
    tlbi vmalle1
    dsb ish
    isb
    ldr x8,[sp,#112]
    str xzr,[x8,#240]
    ldp x19,x20,[sp,#0]
    ldp x21,x22,[sp,#16]
    ldp x23,x24,[sp,#32]
    ldp x25,x26,[sp,#48]
    ldp x27,x28,[sp,#64]
    ldp x29,x30,[sp,#80]
    add sp,sp,#128
    ret
.section .text.user_return,"ax"
.balign 4096
.global fx_c110_user_return
fx_c110_user_return:
    svc #0x4652
    .space 4092
