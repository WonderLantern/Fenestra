/* Original Nikon C1.10 PTP callbacks, never a replacement USB controller.
 * No maintenance operations, shell tooling, Flash, or hidden boot worker. */
#include "fenestra_native_ptp.h"
#include "fenestra_memory.h"
#include "fenestra_c110_demo.h"
#include "fenestra_c110_usb.h"
#include "fenestra_c110_rtos.h"
#include "fenestra_c110_observation.h"
#include "c110_debug_identity.h"
#include <stddef.h>
#define FN(a,t) ((t)(uintptr_t)(a))
static const FxPtpIdentity identity={110u,FX_PTP_CAPABILITIES,3u|FX_PTP_FLAG_CONTROL,"Nikon ZR","C 1.10",
    fx_c110_debug_base,fx_c110_debug_package,fx_c110_debug_identity};
void fx_c110_ptp_init(FxPtp *p) {fx_ptp_init_target(p,&identity);}
extern const FxNativePtpDescriptor *fx_c110_ptp_original_lookup(uint32_t);
extern void fx_c110_ptp_original_session(uint32_t);
extern uint32_t fx_c110_ptp_original_operations(uint8_t *,uint32_t,const uint8_t *);
static void disconnect(void) {fx_c110_observation_disconnect();fx_c110_usb_revoke();}
const FxNativePtpProfile fx_native_ptp_profile={
    fx_c110_menu_ptp,fx_c110_menu_ptp_active,
    FN(UINT64_C(0x408391d0),uint8_t *(*)(void)),
    FN(UINT64_C(0x4083920c),uint32_t (*)(void)),
    FN(UINT64_C(0x408392b4),const uint8_t *(*)(void)),
    FN(UINT64_C(0x408392c0),void (*)(const uint8_t *,uint32_t,uint32_t,const void *,uint32_t)),
    FN(UINT64_C(0x408395b0),uint32_t (*)(const uint8_t *,const uint8_t *,uint32_t,uint32_t)),
    fx_c110_ptp_original_lookup,fx_c110_ptp_original_session,fx_c110_ptp_original_operations,disconnect,
    (const volatile uint32_t *)(uintptr_t)UINT64_C(0xa055be684),
    (const uint8_t *)(uintptr_t)UINT64_C(0xa055bef50),0x200u};
const FxNativePtpDescriptor *fx_c110_ptp_lookup(uint32_t opcode) {return fx_native_ptp_lookup(opcode);}
void fx_c110_ptp_session(uint32_t session) {fx_native_ptp_session(session);}
static uint32_t backend(void *context,uint16_t op,const uint8_t *in,uint32_t bytes,
                        uint8_t *out,uint32_t *count) {
    if(op==FX_PTP_ECHO) { memcpy(out,in,bytes);*count=bytes;return FX_PTP_OK; }
    if(op==FX_PTP_LINUX_BRIDGE)return fx_c110_usb_request(in,bytes,out,count);
    if(op==FX_PTP_RTOS_MONITOR)return fx_c110_rtos_debug(in,bytes,out,count);
    if(op==FX_PTP_OBSERVATION)return fx_c110_observation_request(in,bytes,out,count);
    if(op==FX_PTP_STATUS && !bytes) {
        memset(out,0,32u);fx_ptp_put32(out,110u);fx_ptp_put32(out+4,(uint32_t)fx_c110_menu_ptp_active());
        fx_ptp_put32(out+8,FX_PTP_CAPABILITIES);
        *count=32u;return FX_PTP_OK;
    }
    return fx_c110_demo_debug(context,op,in,bytes,out,count);
}
void fx_c110_ptp_poll(FxC110Demo *demo) {
    FxPtp *p=fx_c110_menu_ptp();
    if(!p) return;
    fx_c110_rtos_tick(atomic_load_explicit(&p->backend_session,memory_order_acquire),
        atomic_load_explicit(&p->generation,memory_order_acquire),
        fx_c110_menu_ptp_active() && atomic_load_explicit(&p->enabled,memory_order_acquire));
    fx_c110_usb_tick(atomic_load_explicit(&p->backend_session,memory_order_acquire),
        atomic_load_explicit(&p->generation,memory_order_acquire),
        fx_c110_menu_ptp_active() && atomic_load_explicit(&p->enabled,memory_order_acquire));
    if(!fx_c110_menu_ptp_active()) {
        if(atomic_load_explicit(&p->enabled,memory_order_acquire)) fx_ptp_enable(p,0);
        return;
    }
    (void)fx_ptp_poll(p,backend,demo);
}
const uint32_t fx_c110_ptp_metadata[]={0x46585032u,2u,sizeof(FxPtp),
    FX_PTP_HELLO,FX_PTP_SUBMIT,FX_PTP_FETCH,FX_PTP_PAYLOAD,FX_PTP_CAPABILITIES};
