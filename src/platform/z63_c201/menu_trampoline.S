.text
.global fx_z63_navigate_original
.type fx_z63_navigate_original,%function
fx_z63_navigate_original:
#include "z63_navigate_original.inc"
    ldr x16,1f
    br x16
    .balign 8
1: .quad 0x403c0474
