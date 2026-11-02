.section .text.boot,"ax"
.global _start
_start:
    msr daifset,#15
    mrs x0,CurrentEL
    cmp x0,#8
    b.ne 1f
    mov x0,#0x80000000
    msr hcr_el2,x0
    mov x0,#3
    msr cnthctl_el2,x0
    msr cntvoff_el2,xzr
    adr x0,1f
    msr elr_el2,x0
    mov x0,#0x3c5
    msr spsr_el2,x0
    eret
1:
    ldr x0,=__stack_end
    mov sp,x0
    ldr x0,=__bss_begin
    ldr x1,=__bss_end
2:  cmp x0,x1
    b.hs 3f
    str xzr,[x0],#8
    b 2b
3:  bl fixture_main
4:  wfi
    b 4b
.section .fixture.allocator,"ax"
    b fixture_allocate
.text
.global memcpy
memcpy:
    b fx_memcpy
.global memset
memset:
    b fx_memset
.global memcmp
memcmp:
    b fx_memcmp
.global fixture_owner_stack
fixture_owner_stack:
    mov sp,x0
    b fixture_work
