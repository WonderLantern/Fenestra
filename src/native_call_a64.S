.text
.balign 4
.global fx_native_aapcs64
.type fx_native_aapcs64,%function
fx_native_aapcs64:
    stp x29,x30,[sp,#-32]!
    stp x19,x20,[sp,#16]
    mov x29,sp
    mov x19,x0
    mov x20,x1
    sub sp,sp,#256
    ldr w11,[x19,#12]
    add x10,x19,#224
    mov x12,sp
    cbz w11,2f
1:
    ldp x13,x14,[x10],#16
    stp x13,x14,[x12],#16
    subs w11,w11,#16
    b.ne 1b
2:
    ldr w9,[x19,#8]
    tbz w9,#0,3f
    ldp q0,q1,[x19,#96]
    ldp q2,q3,[x19,#128]
    ldp q4,q5,[x19,#160]
    ldp q6,q7,[x19,#192]
3:
    ldp x0,x1,[x19,#16]
    ldp x2,x3,[x19,#32]
    ldp x4,x5,[x19,#48]
    ldp x6,x7,[x19,#64]
    ldr x8,[x19,#80]
    blr x20
    stp x0,x1,[x19,#480]
    ldr w9,[x19,#8]
    tbz w9,#0,4f
    stp q0,q1,[x19,#496]
    stp q2,q3,[x19,#528]
4:
    add sp,sp,#256
    ldp x19,x20,[sp,#16]
    ldp x29,x30,[sp],#32
    ret
.size fx_native_aapcs64,.-fx_native_aapcs64
