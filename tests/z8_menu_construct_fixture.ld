ENTRY(_start)
SECTIONS {
    . = 0x401a0000;
    .text : { KEEP(*(.text.boot)) *(.text*) }
    .rodata : { *(.rodata*) }
    ASSERT(. < 0x40200000, "unit fixture exceeds Core window")
    . = 0x40400000;
    .data : { *(.data*) }
    .bss (NOLOAD) : ALIGN(4096) {
        __bss_begin = .; *(.bss*) *(COMMON) . = ALIGN(16); __bss_end = .;
    }
    . += 0x10000; __stack_end = .;
    . = 0x43bf0680;
    .navtbl : { KEEP(*(.navtbl)) }
    .navbad 0x43bf1000 : { KEEP(*(.navbad)) }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
