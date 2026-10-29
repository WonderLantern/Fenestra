/* C5.32 original PTP dispatcher. Backend capabilities describe implemented
 * operations only; ZR-specific capture/Linux addresses never enter this port. */
#include "ptp.h"
#include "fenestra_memory.h"
#include "fenestra_native_rtos.h"
#include "fenestra_native_linux.h"
#include "z9_debug_identity.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#define CAPS ((1u<<(FX_PTP_STATUS-1u))|(1u<<(FX_PTP_ECHO-1u))|(1u<<(FX_PTP_IDENTITY-1u))|(1u<<(FX_PTP_RTOS_MONITOR-1u))|(1u<<(FX_PTP_LINUX_BRIDGE-1u)))
static const FxPtpIdentity identity={532u,CAPS,3u|FX_PTP_FLAG_CONTROL,"Nikon Z9","C 5.32",
    fx_z9_debug_application,fx_z9_debug_application,fx_z9_debug_build};
extern const FxNativePtpDescriptor *fx_z9_ptp_original_lookup(uint32_t);
extern void fx_z9_ptp_original_session(uint32_t);
extern uint32_t fx_z9_ptp_original_operations(uint8_t *,uint32_t,const uint8_t *);
const FxNativePtpProfile fx_native_ptp_profile={
    fx_z9_menu_ptp,fx_z9_menu_ptp_active,
    FN(UINT64_C(0x407e337c),uint8_t *(*)(void)),
    FN(UINT64_C(0x407e33b8),uint32_t (*)(void)),
    FN(UINT64_C(0x407e3460),const uint8_t *(*)(void)),
    FN(UINT64_C(0x407e346c),void (*)(const uint8_t *,uint32_t,uint32_t,const void *,uint32_t)),
    FN(UINT64_C(0x407e375c),uint32_t (*)(const uint8_t *,const uint8_t *,uint32_t,uint32_t)),
    fx_z9_ptp_original_lookup,fx_z9_ptp_original_session,fx_z9_ptp_original_operations,fx_linux_revoke,
    (const volatile uint32_t *)(uintptr_t)UINT64_C(0xa04a756c4),
    (const uint8_t *)(uintptr_t)UINT64_C(0xa04a75f48),0x200u};
void fx_z9_ptp_init(FxPtp *p) {fx_ptp_init_target(p,&identity);}
const FxNativePtpDescriptor *fx_z9_ptp_lookup(uint32_t op) {return fx_native_ptp_lookup(op);}
void fx_z9_ptp_session(uint32_t session) {fx_native_ptp_session(session);}
uint32_t fx_z9_ptp_operations(uint8_t *out,uint32_t capacity,const uint8_t *operations) {
    return fx_native_ptp_operations(out,capacity,operations);
}
static uint32_t backend(void *context,uint16_t op,const uint8_t *in,uint32_t bytes,
                        uint8_t *out,uint32_t *count) {
    (void)context;
    if(op==FX_PTP_ECHO) {memcpy(out,in,bytes);*count=bytes;return FX_PTP_OK;}
    if(op==FX_PTP_RTOS_MONITOR)return fx_rtos_debug(in,bytes,out,count);
    if(op==FX_PTP_LINUX_BRIDGE)return fx_linux_request(in,bytes,out,count);
    if(op==FX_PTP_STATUS && !bytes) {
        memset(out,0,32u);fx_ptp_put32(out,532u);
        fx_ptp_put32(out+4,(uint32_t)fx_z9_menu_ptp_active());fx_ptp_put32(out+8,CAPS);
        *count=32u;return FX_PTP_OK;
    }
    return FX_PTP_UNSUPPORTED;
}
void fx_z9_ptp_poll(void) {
    FxPtp *p=fx_z9_menu_ptp();if(!p)return;
    fx_linux_tick(atomic_load_explicit(&p->backend_session,memory_order_acquire),
        atomic_load_explicit(&p->generation,memory_order_acquire),
        fx_z9_menu_ptp_active() && atomic_load_explicit(&p->enabled,memory_order_acquire));
    if(!fx_z9_menu_ptp_active()) {
        if(atomic_load_explicit(&p->enabled,memory_order_acquire))fx_ptp_enable(p,0);
        return;
    }
    (void)fx_ptp_poll(p,backend,0);
}
