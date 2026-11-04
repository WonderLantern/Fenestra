ENTRY(fx_z63_system_dispatch)
SECTIONS {
    . = 0x40910000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    ASSERT(. <= 0x40c00000, "Z63 mapped image padding exhausted")
    ASSERT(SIZEOF(.data) == 0, "Probe must have no writable static data")
    ASSERT(SIZEOF(.bss) == 0, "Probe must have no writable static data")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
