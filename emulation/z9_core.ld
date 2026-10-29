ENTRY(fx_z9_menu_page)
SECTIONS {
    . = 0x401a0000;
    __fx_core_code_begin = .;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    __fx_core_code_end = .;
    ASSERT(. <= 0x40200000, "Z9 original RX padding exhausted")
    ASSERT(SIZEOF(.data) == 0, "Core must not contain writable static data")
    ASSERT(SIZEOF(.bss) == 0, "Core must use owned heap RAM")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
