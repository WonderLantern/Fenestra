ENTRY(_start)
SECTIONS {
    . = 0x401a0000;
    .text : { KEEP(*(.text.boot)) *(.text*) }
    .rodata : { *(.rodata*) }
    ASSERT(. < 0x40200000, "gate fixture code exceeds original Core window")
    . = 0x40400000;
    .data : { *(.data*) }
    .bss (NOLOAD) : ALIGN(4096) {
        __bss_begin = .; *(.bss*) *(COMMON) . = ALIGN(16); __bss_end = .;
    }
    . = ALIGN(4096);
    . += 0x10000;
    __stack_end = .;
    . = 0x40d31898;
    .allocator : { KEEP(*(.fixture.allocator)) }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
