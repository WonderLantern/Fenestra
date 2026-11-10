/* Z8 port derived from upstream src/platform/z9_c532/rtos_printf.S.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#include "../../native_rtos_printf.inc"
.text
FX_RTOS_PRINTF fx_z8_rtos_printf,fx_rtos_vprintf,fx_z8_rtos_original_printf
.global fx_z8_rtos_original_printf
.type fx_z8_rtos_original_printf,%function
fx_z8_rtos_original_printf:
#include "fx_z8_rtos_original_printf.inc"
    ldr x16,2f
    br x16
    .balign 8
2: .quad FX_Z8_CONT_RTOS_PRINTF
