ENTRY(fx_z63_system_dispatch)
SECTIONS {
    /* In-place executable padding. Original startup never remaps it; see
     * host/z63_placement.py. The builder supplies both bounds from the profile. */
    . = fx_z63_core_begin;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    ASSERT(. <= fx_z63_core_end, "Z63 in-place image padding exhausted")
    ASSERT(SIZEOF(.data) == 0, "Probe must have no writable static data")
    ASSERT(SIZEOF(.bss) == 0, "Probe must have no writable static data")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
