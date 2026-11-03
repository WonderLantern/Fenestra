ENTRY(_start)
SECTIONS {
    . = 0x401a0000;
    .text : { KEEP(*(.text.boot)) *(.text*) }
    .rodata : { *(.rodata*) }
    ASSERT(. < 0x40200000, "session fixture exceeds Core window")
    . = 0x40400000;
    .data : { *(.data*) }
    .bss (NOLOAD) : ALIGN(4096) {
        __bss_begin = .; *(.bss*) *(COMMON) . = ALIGN(16); __bss_end = .;
    }
    . += 0x10000; __stack_end = .;
    /* Firmware-address shims: the production Z8 card and heap wrappers call
     * these original image addresses directly; the fixture maps branch stubs
     * onto them so the rest of the load path stays real. */
    . = 0x402daa4c; .fcbopen  : { KEEP(*(.fcb.open)) }
    . = 0x402daaac; .fcbclose : { KEEP(*(.fcb.close)) }
    . = 0x402dab4c; .fcbread  : { KEEP(*(.fcb.read)) }
    . = 0x40d2c408; .fcballoc : { KEEP(*(.fcb.alloc)) }
    . = 0x40d2c448; .fcbfree  : { KEEP(*(.fcb.free)) }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
