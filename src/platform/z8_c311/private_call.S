/* Normal call boundary for an owned EL1 module view in private QEMU.
 * Saves/restores the caller's native system registers and interrupt mask.
 * No kernel task frame, PC register or firmware translation table is patched.
 */
#include "fenestra_simd.h"
.text
.global fx_z8_private_call
.type fx_z8_private_call,%function
fx_z8_private_call:
    sub sp,sp,#FX_PRIVATE_FRAME_BYTES
    stp x19,x20,[sp]
    stp x21,x22,[sp,#16]
    stp x23,x24,[sp,#32]
    stp x25,x26,[sp,#48]
    stp x27,x28,[sp,#64]
    stp x29,x30,[sp,#80]
    mov x19,x0
    mov x20,x1
    mov x21,x2
    mov x8,sp
    str x8,[x21,#232] /* Protected outer frame; EL0 owns SP_EL0. */
    mrs x8,daif
    mrs x9,ttbr0_el1
    stp x8,x9,[sp,#96]
    msr daifset,#15
    mrs x8,ttbr1_el1
    mrs x9,tcr_el1
    stp x8,x9,[sp,#112]
    mrs x8,mair_el1
    mrs x9,vbar_el1
    stp x8,x9,[sp,#128]
    mrs x8,spsel
    str x8,[sp,#160]
    /* Native tasks use EL1t/SP_EL0. Keep SP_EL0 as a stable exception
     * anchor, and use the same owned task stack through SP_EL1 here.
     * Save the original inactive kernel stack before selecting this view. */
    mov x10,sp
    cbnz x8,1f
    msr spsel,#1
    mov x9,sp
    mov sp,x10
    str x9,[sp,#168]
1:
    /* SP_EL0 sysreg access is undefined while SPSel==0. Read it only
     * after selecting SP_EL1; the caller's EL1t frame is now inactive. */
    mrs x8,tpidrro_el0
    mrs x9,sp_el0
    stp x8,x9,[sp,#144]
    mrs x8,sctlr_el1
    str x8,[sp,#176]
    bic x8,x8,#0x200 /* EL0 must not change DAIF through SCTLR.UMA. */
    msr sctlr_el1,x8
    mrs x8,cpacr_el1
    str x8,[sp,#184]
    bic x8,x8,#0x300000
    orr x8,x8,#0x100000 /* EL1 always permits the protected save/restore. */
    ldr x9,[x21,#FX_MODULE_SIMD_ENABLE_OFFSET]
    cbz x9,5f
    orr x8,x8,#0x200000 /* Explicit module capability permits EL0 SIMD/FP. */
5:
    msr cpacr_el1,x8
    isb
    fx_simd_save sp,192
    mov x8,sp
    msr sp_el0,x8
    msr tpidrro_el0,x21
    adrp x8,fx_z8_private_vectors
    add x8,x8,:lo12:fx_z8_private_vectors
    msr vbar_el1,x8
    mov x8,#0xff
    msr mair_el1,x8
    mov x8,#0x3519
    movk x8,#0x80,lsl #16
    movk x8,#2,lsl #32
    msr tcr_el1,x8
    msr ttbr1_el1,xzr
    msr ttbr0_el1,x19
    dsb ish
    tlbi vmalle1
    dsb ish
    isb
    mov x0,x21
    blr x20
fx_z8_private_restore:
    mov x19,x0
    fx_simd_restore sp,192
    ldr x8,[sp,#176]
    msr sctlr_el1,x8
    ldr x8,[sp,#184]
    msr cpacr_el1,x8
    ldp x8,x9,[sp,#112]
    msr ttbr1_el1,x8
    msr tcr_el1,x9
    ldp x8,x9,[sp,#128]
    msr mair_el1,x8
    msr vbar_el1,x9
    ldp x8,x9,[sp,#96]
    msr ttbr0_el1,x9
    dsb ish
    tlbi vmalle1
    dsb ish
    isb
    ldp x10,x11,[sp,#144]
    msr tpidrro_el0,x10
    msr sp_el0,x11
    ldp x12,x13,[sp,#160]
    mov x0,x19
    ldp x19,x20,[sp,#0]
    ldp x21,x22,[sp,#16]
    ldp x23,x24,[sp,#32]
    ldp x25,x26,[sp,#48]
    ldp x27,x28,[sp,#64]
    ldp x29,x30,[sp,#80]
    cbnz x12,2f
    mov sp,x13
    msr spsel,#0
2:
    add sp,sp,#FX_PRIVATE_FRAME_BYTES
    msr daif,x8
    ret
.balign 2048
fx_z8_private_vectors:
.rept 8
    b fx_z8_private_exception
    .space 124
.endr
    b fx_z8_private_exception /* lower EL AArch64 synchronous */
    .space 124
    b fx_z8_private_irq       /* lower EL AArch64 IRQ */
    .space 124
.rept 6
    b fx_z8_private_exception
    .space 124
.endr
fx_z8_private_irq:
    /* Do not acknowledge GIC or enter the native handler on private tables.
     * Save only EL0-owned state into protected memory and unwind our gate.
     * The still-pending IRQ is handled natively once original DAIF is restored. */
    stp x0,x1,[sp,#-16]!
    mrs x0,tpidrro_el0
    add x0,x0,#272
    stp x2,x3,[x0,#16]
    stp x4,x5,[x0,#32]
    stp x6,x7,[x0,#48]
    stp x8,x9,[x0,#64]
    stp x10,x11,[x0,#80]
    stp x12,x13,[x0,#96]
    stp x14,x15,[x0,#112]
    stp x16,x17,[x0,#128]
    stp x18,x19,[x0,#144]
    stp x20,x21,[x0,#160]
    stp x22,x23,[x0,#176]
    stp x24,x25,[x0,#192]
    stp x26,x27,[x0,#208]
    stp x28,x29,[x0,#224]
    str x30,[x0,#240]
    fx_simd_save x0,(FX_MODULE_SIMD_OFFSET-272)
    ldp x1,x2,[sp]
    stp x1,x2,[x0]
    mrs x1,sp_el0
    mrs x2,elr_el1
    stp x1,x2,[x0,#248]
    mrs x1,spsr_el1
    mov x2,#1
    stp x1,x2,[x0,#264]
    ldr x1,[x0,#280]
    add x1,x1,#1
    str x1,[x0,#280]
    sub x0,x0,#272
    ldr x1,[x0,#232]
    mov sp,x1
    mov x0,#13 /* FX_INTERRUPTED; no CPU fault was asserted. */
    b fx_z8_private_restore
fx_z8_private_exception:
    mov x4,x0 /* SVC argument, ignored for other exception classes. */
    mrs x0,tpidrro_el0
    mrs x1,esr_el1
    mrs x2,elr_el1
    mrs x3,far_el1
    mrs x5,spsr_el1
    str x5,[x0,#248]
    str x2,[x0,#256]
    /* Only our read-only return trampoline can resume the EL1 gate. */
    lsr x5,x1,#26
    cmp x5,#0x15
    b.ne 3f
    and x5,x1,#0xffff
    mov x6,#0x4652
    cmp x5,x6
    b.ne 3f
    mrs x5,spsr_el1
    and x5,x5,#0x1f
    cbnz x5,3f
    adrp x5,fx_z8_user_return
    add x5,x5,:lo12:fx_z8_user_return
    add x5,x5,#4
    cmp x2,x5
    b.ne 3f
    ldr x8,[x0,#240]
    cbz x8,3f
    ldr w5,[x0,#268]
    add w5,w5,#1
    str w5,[x0,#268]
    /* A resumed invocation has now COMPLETED. Leaving pending set makes
     * operation() mistake this real SVC return for another IRQ unwind and
     * repeatedly execute the old continuation without dispatching replies. */
    str xzr,[x0,#544]
    mov sp,x8
    mov x0,x4
    b fx_z8_user_resume
3:
    ldr x8,[x0,#232]
    mov sp,x8
    bl fx_z8_private_fault
    mov x0,#10 /* FX_INTEGRITY: a synchronous CPU exception is not a timeout. */
    b fx_z8_private_restore
