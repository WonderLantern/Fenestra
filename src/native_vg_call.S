/* AAPCS64 bridge: target from a checked model profile, integer API at entry. */
.text
.global fx_native_vg_create_path
.type fx_native_vg_create_path,%function
fx_native_vg_create_path:
    mov x16,x0
    mov w0,#0
    mov w1,#3
    fmov s0,#1.0
    fmov s1,wzr
    mov w2,#0
    mov w3,#0
    mov w4,#0xfff
    br x16
.size fx_native_vg_create_path,.-fx_native_vg_create_path
