#include "../../native_rtos_printf.inc"
.text
FX_RTOS_PRINTF fx_z63_ptp_rtos_printf,fx_rtos_vprintf,fx_z63_ptp_original_rtos_printf
.global fx_z63_ptp_original_rtos_printf
.type fx_z63_ptp_original_rtos_printf,%function
fx_z63_ptp_original_rtos_printf:
#include "fx_z63_ptp_original_rtos_printf.inc"
    ldr x16,1f
    br x16
    .balign 8
1: .quad 0x40c05354
