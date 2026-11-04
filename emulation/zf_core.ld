/* QEMU experiment only: the entry is an ABI-matched SYSCTL wrapper, not
 * the first function in the input object ordering. */
ENTRY(fx_zf_system_dispatch)
SECTIONS {
    . = 0x401a0000;
    __fx_core_code_begin = .;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    __fx_core_code_end = .;
    ASSERT(DEFINED(fx_zf_system_dispatch), "Zf SYSCTL wrapper missing")
    ASSERT(. <= 0x40200000, "Zf mapped RX padding exhausted")
    ASSERT(SIZEOF(.data) == 0, "Core must not contain writable static data")
    ASSERT(SIZEOF(.bss) == 0, "Core must use owned heap RAM")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
