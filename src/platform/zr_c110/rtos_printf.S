/* Preserve the COMPLETE AAPCS64 variadic register save area. The C sources use
 * -mgeneral-regs-only, so a C variadic wrapper would lose floating arguments.
 * No capture: restore every argument and tail-call the displaced prologue. */
#include "../../native_rtos_printf.inc"
.text
FX_RTOS_PRINTF fx_c110_rtos_printf,fx_c110_rtos_vprintf,fx_c110_rtos_original_printf
.global fx_c110_rtos_original_printf
.type fx_c110_rtos_original_printf,%function
fx_c110_rtos_original_printf:
#include "fx_c110_rtos_original_printf.inc"
    ldr x16,2f
    br x16
    .balign 8
2:  .quad 0x40e05720
