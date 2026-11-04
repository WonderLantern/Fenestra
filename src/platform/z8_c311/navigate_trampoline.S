.text
.global fx_z8_navigate_original
.type fx_z8_navigate_original,%function
fx_z8_navigate_original:
    .inst 0xf81e0ff4
    .inst 0xa9017bf3
    .inst 0xaa0103f4
    .inst 0x2a0003f3
    ldr x16,1f
    br x16
    .balign 8
1: .quad 0x4039e820
