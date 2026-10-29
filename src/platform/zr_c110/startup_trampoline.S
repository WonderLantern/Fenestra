.text
.global fx_c110_system_original
.type fx_c110_system_original,%function
fx_c110_system_original:
#include "fx_c110_system_original.inc"
    ldr x16,1f
    br x16
    .balign 8
1:  .quad 0x40f760f4
.global fx_c110_navigate_original
.type fx_c110_navigate_original,%function
fx_c110_navigate_original:
#include "fx_c110_navigate_original.inc"
    ldr x16,2f
    br x16
    .balign 8
2:  .quad 0x403bba88
