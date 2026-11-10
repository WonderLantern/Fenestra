/* Z8 port derived from upstream src/platform/z9_c532/startup_trampoline.S.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
.text
.global fx_z8_system_original
.type fx_z8_system_original,%function
fx_z8_system_original:
#include "fx_z8_system_original.inc"
    ldr x16,1f
    br x16
    .balign 8
1: .quad FX_Z8_CONT_SYSTEM
.global fx_z8_navigate_original
.type fx_z8_navigate_original,%function
fx_z8_navigate_original:
#include "fx_z8_navigate_original.inc"
    ldr x16,2f
    br x16
    .balign 8
2: .quad FX_Z8_CONT_NAVIGATE
