/* Complete overwritten C1.10 getter prefix; no PC-relative instruction.
 * Fixed resume into the unchanged native text getter.
 */
.text
.global fx_c110_original_text
.type fx_c110_original_text,%function
fx_c110_original_text:
#include "fx_c110_original_text.inc"
    b fx_c110_native_text_resume
