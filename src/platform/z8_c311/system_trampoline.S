.text
.global fx_z8_system_original
.type fx_z8_system_original,%function
fx_z8_system_original:
    .inst 0xa9bc63f9
    .inst 0xa9015bf7
    .inst 0xa90253f5
    .inst 0xa9037bf3
    ldr x16,1f
    br x16
    .balign 8
1: .quad 0x40d60ed8
