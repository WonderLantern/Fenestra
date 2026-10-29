ENTRY(fx_c110_menu_page)
SECTIONS {
    /* Explicit private-QEMU ROM reservation in C1.10's component alignment
     * padding. No stock MMU mutation and no change to boot instructions. */
    . = 0x401a0000;
    __fx_core_code_begin = .;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    __fx_core_code_end = .;
    __fx_c110_code_end = .;
    ASSERT(__fx_c110_code_end <= 0x40200000, "Native core exceeds verified ROM padding")
    ASSERT(SIZEOF(.data) == 0, "Native menu must not contain writable static data")
    ASSERT(SIZEOF(.bss) == 0, "Native menu must use normal heap ownership")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
