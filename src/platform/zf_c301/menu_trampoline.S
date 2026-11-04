.text
.balign 4
.global fx_zf_menu_text_original
.type fx_zf_menu_text_original,%function
fx_zf_menu_text_original:
/* First four instructions are MOV/MOVK only, checked by the patch builder. */
#include "fx_zf_menu_text_prefix.inc"
    ldr x16, 1f
    br x16
.balign 8
1: .quad 0x40338ef8
.balign 4
.global fx_zf_menu_navigate_original
.type fx_zf_menu_navigate_original,%function
fx_zf_menu_navigate_original:
/* STR/STP/STP/MOV prologue, before the first PC-relative call. */
#include "fx_zf_menu_navigate_prefix.inc"
    ldr x16, 2f
    br x16
.balign 8
2: .quad 0x403b8034
