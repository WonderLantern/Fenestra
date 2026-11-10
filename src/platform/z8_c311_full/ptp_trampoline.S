/* Z8 port derived from upstream src/platform/z9_c532/ptp_trampoline.S.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
/* Original C3.11 preimages checked against the entire pinned MAIN. */
.text
.global fx_z8_ptp_original_lookup
.type fx_z8_ptp_original_lookup,%function
fx_z8_ptp_original_lookup:
#include "fx_z8_ptp_original_lookup.inc"
    ldr x16,1f
    br x16
    .balign 8
1: .quad FX_Z8_CONT_PTP_LOOKUP
.global fx_z8_ptp_original_session
.type fx_z8_ptp_original_session,%function
fx_z8_ptp_original_session:
#include "fx_z8_ptp_original_session.inc"
    ldr x16,2f
    br x16
    .balign 8
2: .quad FX_Z8_CONT_PTP_SESSION
.global fx_z8_ptp_original_operations
.type fx_z8_ptp_original_operations,%function
fx_z8_ptp_original_operations:
#include "fx_z8_ptp_original_operations.inc"
    ldr x16,3f
    br x16
    .balign 8
3: .quad FX_Z8_CONT_PTP_OPERATIONS
