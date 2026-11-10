/* Preimages are generated privately from the exact Zf C3.01 original EG. */
.text
.global fx_zf_ptp_original_lookup
.type fx_zf_ptp_original_lookup,%function
fx_zf_ptp_original_lookup:
#include "fx_zf_ptp_original_lookup.inc"
    ldr x16,1f
    br x16
    .balign 8
1: .quad 0x407f5010
.global fx_zf_ptp_original_session
.type fx_zf_ptp_original_session,%function
fx_zf_ptp_original_session:
#include "fx_zf_ptp_original_session.inc"
    ldr x16,2f
    br x16
    .balign 8
2: .quad 0x407f39c8
.global fx_zf_ptp_original_operations
.type fx_zf_ptp_original_operations,%function
fx_zf_ptp_original_operations:
#include "fx_zf_ptp_original_operations.inc"
    ldr x16,3f
    br x16
    .balign 8
3: .quad 0x40361d18
