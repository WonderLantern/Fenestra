.text
.global fx_c110_vg_create_path
.type fx_c110_vg_create_path,%function
fx_c110_vg_create_path:
    ldr x0,=0x40252f0c
    b fx_native_vg_create_path
.size fx_c110_vg_create_path,.-fx_c110_vg_create_path
