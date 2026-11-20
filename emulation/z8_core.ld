ENTRY(fx_z8_menu_page)
SECTIONS {
    . = FX_Z8_CODE_BEGIN;
    __fx_core_code_begin = .;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    __fx_core_code_end = .;
    ASSERT(. <= FX_Z8_CODE_END, "Z8 original RX padding exhausted")
    ASSERT(SIZEOF(.data) == 0, "Core must not contain writable static data")
    ASSERT(SIZEOF(.bss) == 0, "Core must use owned heap RAM")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
