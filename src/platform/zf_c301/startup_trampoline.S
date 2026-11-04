#if !defined(FX_ZF_EXPERIMENTAL_SYSCTL_GATE) || FX_ZF_EXPERIMENTAL_SYSCTL_GATE != 1
#error "Zf SYSCTL trampoline is experimental and is not enabled by a production builder"
#endif

/* The include is generated locally from the pinned original Zf application. */
.text
.global fx_zf_system_original
.type fx_zf_system_original,%function
fx_zf_system_original:
#include "fx_zf_system_original.inc"
    ldr x16, 1f
    br x16
    .balign 8
1:  .quad 0x40d64e8c
