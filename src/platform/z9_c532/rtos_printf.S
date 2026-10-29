#include "../../native_rtos_printf.inc"
.text
FX_RTOS_PRINTF fx_z9_rtos_printf,fx_rtos_vprintf,fx_z9_rtos_original_printf
.global fx_z9_rtos_original_printf
.type fx_z9_rtos_original_printf,%function
fx_z9_rtos_original_printf:
#include "fx_z9_rtos_original_printf.inc"
    ldr x16,2f
    br x16
    .balign 8
2: .quad 0x40c05724
