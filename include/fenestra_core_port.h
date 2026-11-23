#ifndef FENESTRA_CORE_PORT_H
#define FENESTRA_CORE_PORT_H

#define FX_CORE_PORT_ABI 1
#define FX_CORE_PORT_MAGIC 0x31504e46

/* Function slots use AAPCS64. Profile slots point to immutable structures.
 * Change either kind of slot only with an ABI revision. */
#define FX_CORE_PORT_IMPORTS(X) \
    X(fx_native_hooks,0) \
    X(fx_native_module_fault,1) \
    X(fx_native_apps_domain,2) \
    X(fx_native_mappings,4) \
    X(fx_native_module_loaded,6) \
    X(fx_native_module_bus,8) \
    X(fx_native_ui_dispatch_domain,10) \
    X(fx_native_apps_flush_native,12) \
    X(fx_native_ui_flush_owner,13)
#define FX_CORE_PORT_PROFILES(X) \
    X(fx_native_display_profile,fx_core_port_display,FxNativeDisplayProfile,3) \
    X(fx_native_linux_profile,fx_core_port_linux,FxNativeLinuxProfile,5) \
    X(fx_native_ptp_profile,fx_core_port_ptp,FxNativePtpProfile,7) \
    X(fx_native_rtos_profile,fx_core_port_rtos,FxNativeRtosProfile,9) \
    X(fx_native_ui_dispatch_profile,fx_core_port_ui,FxNativeUiDispatchProfile,11)
#define FX_CORE_PORT_IMPORT_COUNT 14
#define FX_CORE_PORT_IMPORT_OFFSET 104
#define FX_CORE_PORT_HEADER_BYTES 216

#ifndef __ASSEMBLER__
#include "fenestra_card_payload.h"

typedef struct {
    uint32_t magic,abi,header_bytes,reserved;
    uint8_t application[32],core[32];
    uint64_t install,attach,dispatch;
    uint64_t imports[FX_CORE_PORT_IMPORT_COUNT];
} FxCorePort;

_Static_assert(offsetof(FxCorePort,imports)==FX_CORE_PORT_IMPORT_OFFSET,"Port imports ABI");
_Static_assert(sizeof(FxCorePort)==FX_CORE_PORT_HEADER_BYTES,"Port header ABI");

FxResult fx_core_port_validate(const void *,uint32_t,uintptr_t,const uint8_t [32],const uint8_t [32]);
void fx_universal_core_start(const FxCardPayloadBoot *);
#endif
#endif
