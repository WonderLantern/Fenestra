/* Fixed C1.10 sensor transport BL/CMP pair. The native driver's LR remains
 * 413522ac. All GP registers/NZCV are restored around integer-only observers;
 * no SIMD/FP state is touched, even in native tasks with FP disabled.
 * Native SPI consumes two register arguments; no stack arguments are moved. */
.text
.macro save
    stp x0,x1,[sp]
    stp x2,x3,[sp,#16]
    stp x4,x5,[sp,#32]
    stp x6,x7,[sp,#48]
    stp x8,x9,[sp,#64]
    stp x10,x11,[sp,#80]
    stp x12,x13,[sp,#96]
    stp x14,x15,[sp,#112]
    stp x16,x17,[sp,#128]
    stp x18,x19,[sp,#144]
    stp x20,x21,[sp,#160]
    stp x22,x23,[sp,#176]
    stp x24,x25,[sp,#192]
    stp x26,x27,[sp,#208]
    stp x28,x29,[sp,#224]
    str x30,[sp,#240]
    mrs x9,nzcv
    str x9,[sp,#248]
    mrs x9,par_el1
    str x9,[sp,#280]
    mrs x9,tpidr_el1
    str x9,[sp,#288]
    mrs x9,daif
    str x9,[sp,#296]
.endm
.macro restore
    ldr x9,[sp,#280]
    msr par_el1,x9 /* Read-only AT probes must not leak PAR into native code. */
    ldr x9,[sp,#248]
    msr nzcv,x9
    ldp x0,x1,[sp]
    ldp x2,x3,[sp,#16]
    ldp x4,x5,[sp,#32]
    ldp x6,x7,[sp,#48]
    ldp x8,x9,[sp,#64]
    ldp x10,x11,[sp,#80]
    ldp x12,x13,[sp,#96]
    ldp x14,x15,[sp,#112]
    ldp x16,x17,[sp,#128]
    ldp x18,x19,[sp,#144]
    ldp x20,x21,[sp,#160]
    ldp x22,x23,[sp,#176]
    ldp x24,x25,[sp,#192]
    ldp x26,x27,[sp,#208]
    ldp x28,x29,[sp,#224]
    ldr x30,[sp,#240]
.endm
.global fx_c110_sensor_trace_pre
.type fx_c110_sensor_trace_pre,%function
fx_c110_sensor_trace_pre:
    sub sp,sp,#320
    save
    mrs x9,tpidrro_el0
    str x9,[sp,#264]
    mrs x9,mpidr_el1
    str x9,[sp,#272]
    mov x0,sp
    bl fx_c110_sensor_trace_enter
    str x0,[sp,#256]
    restore
    adrp x30,fx_c110_sensor_trace_native_return
    add x30,x30,:lo12:fx_c110_sensor_trace_native_return
    b fx_c110_sensor_trace_native_spi
.size fx_c110_sensor_trace_pre,.-fx_c110_sensor_trace_pre
.global fx_c110_sensor_trace_post
.type fx_c110_sensor_trace_post,%function
fx_c110_sensor_trace_post:
    save
    mrs x9,tpidrro_el0
    str x9,[sp,#264]
    mrs x9,mpidr_el1
    str x9,[sp,#272]
    mov x0,sp
    bl fx_c110_sensor_trace_leave
    restore
    add sp,sp,#320
    cmp w0,#0 /* Exact original instruction displaced at413522ac. */
    b fx_c110_sensor_trace_native_after_cmp
.size fx_c110_sensor_trace_post,.-fx_c110_sensor_trace_post
