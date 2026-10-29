/* Original C5.32 preimages checked against the entire pinned MAIN. */
.text
.global fx_z9_ptp_original_lookup
.type fx_z9_ptp_original_lookup,%function
fx_z9_ptp_original_lookup:
#include "fx_z9_ptp_original_lookup.inc"
    ldr x16,1f
    br x16
    .balign 8
1: .quad 0x407e2e1c
.global fx_z9_ptp_original_session
.type fx_z9_ptp_original_session,%function
fx_z9_ptp_original_session:
#include "fx_z9_ptp_original_session.inc"
    ldr x16,2f
    br x16
    .balign 8
2: .quad 0x407e1844
.global fx_z9_ptp_original_operations
.type fx_z9_ptp_original_operations,%function
fx_z9_ptp_original_operations:
#include "fx_z9_ptp_original_operations.inc"
    ldr x16,3f
    br x16
    .balign 8
3: .quad 0x4035706c
