#include "fenestra_core_port.h"

.text
.macro FX_PORT_GATE name,slot
.p2align 2
.global \name
.type \name,%function
\name:
    adrp x16,__fx_native_port
    add x16,x16,:lo12:__fx_native_port
    ldr x16,[x16,#(FX_CORE_PORT_IMPORT_OFFSET + 8 * \slot)]
    br x16
.size \name,.-\name
.endm

#define FX_PORT_DECLARE(name,slot) FX_PORT_GATE name,slot ;
FX_CORE_PORT_IMPORTS(FX_PORT_DECLARE)

.macro FX_PORT_PTR accessor,slot
.p2align 2
.global \accessor
.type \accessor,%function
\accessor:
    adrp x0,__fx_native_port
    add x0,x0,:lo12:__fx_native_port
    ldr x0,[x0,#(FX_CORE_PORT_IMPORT_OFFSET + 8 * \slot)]
    ret
.size \accessor,.-\accessor
.endm

#define FX_PORT_DECLARE_PROFILE(object,accessor,type,slot) FX_PORT_PTR accessor,slot ;
FX_CORE_PORT_PROFILES(FX_PORT_DECLARE_PROFILE)
