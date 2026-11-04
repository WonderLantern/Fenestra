.section .rodata.fixture_package,"a"
.global fixture_package_begin
.global fixture_package_end
fixture_package_begin:
#ifndef FX_GATE_PACKAGE
#error "Supply the freshly built fixture WLM path"
#endif
    .incbin FX_GATE_PACKAGE
fixture_package_end:
