.text
.global fx_z8_system_original
.type fx_z8_system_original,%function
fx_z8_system_original:
    .inst 0xd10103ff
    .inst 0xa9037bf3
    bl fx_z8_system_bl_placeholder
fx_z8_system_bl_placeholder:
    .inst 0xd28fd881
    ldr x16,1f
    br x16
    .balign 8
1: .quad 0x40fac954
