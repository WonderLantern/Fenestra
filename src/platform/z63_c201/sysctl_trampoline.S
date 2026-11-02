.text
.global fx_z63_system_original
.type fx_z63_system_original,%function
fx_z63_system_original:
#include "z63_system_original.inc"
    ldr x16,1f
    br x16
    .balign 8
1: .quad 0x40d66f00
