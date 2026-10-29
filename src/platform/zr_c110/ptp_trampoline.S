/* Displaced instructions are verified against official C1.10 at build time.
 * x16 is an AAPCS64 scratch register. Original arguments and LR are retained. */
.text
.global fx_c110_ptp_original_lookup
.type fx_c110_ptp_original_lookup,%function
fx_c110_ptp_original_lookup:
#include "fx_c110_ptp_original_lookup.inc"
    ldr x16,1f
    br x16
    .balign 8
1:  .quad 0x40838bf0
.global fx_c110_ptp_original_session
.type fx_c110_ptp_original_session,%function
fx_c110_ptp_original_session:
#include "fx_c110_ptp_original_session.inc"
    ldr x16,2f
    br x16
    .balign 8
2:  .quad 0x40837620
.global fx_c110_ptp_original_operations
.type fx_c110_ptp_original_operations,%function
fx_c110_ptp_original_operations:
#include "fx_c110_ptp_original_operations.inc"
    ldr x16,3f
    br x16
    .balign 8
3:  .quad 0x403638f4
