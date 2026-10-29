/* Shared AAPCS64 integer, SIMD and stack variadic capture. */
.macro FX_RTOS_PRINTF entry,capture,original
.global \entry
.type \entry,%function
\entry:
    sub sp,sp,#256
    stp x0,x1,[sp,#0]
    stp x2,x3,[sp,#16]
    stp x4,x5,[sp,#32]
    stp x6,x7,[sp,#48]
    stp q0,q1,[sp,#64]
    stp q2,q3,[sp,#96]
    stp q4,q5,[sp,#128]
    stp q6,q7,[sp,#160]
    str x30,[sp,#224]
    add x2,sp,#256
    add x3,sp,#64
    stp x2,x3,[sp,#192]
    add x3,sp,#192
    str x3,[sp,#208]
    mov w3,#-56
    mov w4,#-128
    stp w3,w4,[sp,#216]
    add x1,sp,#192
    bl \capture
    cbz w0,1f
    ldr x30,[sp,#224]
    add sp,sp,#256
    mov w0,#0
    ret
1:  ldp x0,x1,[sp,#0]
    ldp x2,x3,[sp,#16]
    ldp x4,x5,[sp,#32]
    ldp x6,x7,[sp,#48]
    ldp q0,q1,[sp,#64]
    ldp q2,q3,[sp,#96]
    ldp q4,q5,[sp,#128]
    ldp q6,q7,[sp,#160]
    ldr x30,[sp,#224]
    add sp,sp,#256
    b \original
.endm
