/* Shared original-dispatcher adapter. No backend runs on USB callbacks. */
#include "fenestra_native_ptp.h"
static uint16_t u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8)); }
static uint32_t response(const uint8_t *request,uint32_t code,uint32_t mode) {
    fx_native_ptp_profile.response
        (request,code,0u,0,mode);
    return code==0x2001u?1u:0u;
}
static uint32_t error_code(uint32_t r) {
    return r==FX_PTP_OK?0x2001u:r==FX_PTP_BUSY?0x2019u:
        r==FX_PTP_CLOSED || r==FX_PTP_UNSUPPORTED?0x2005u:0x201du;
}
static uint32_t buffer(const FxNativePtpDescriptor *d,const uint8_t *q,uint8_t **out,uint32_t *bytes) {
    (void)d;(void)q;
    *out=fx_native_ptp_profile.payload();
    uint32_t capacity=fx_native_ptp_profile.capacity();
    *bytes=capacity<FX_PTP_FRAME?capacity:FX_PTP_FRAME;
    return *out && *bytes==FX_PTP_FRAME?1u:0u;
}
static uint32_t preflight(const FxNativePtpDescriptor *d,const uint8_t *q) {
    uint32_t mode=d->direction==1u?2u:0u;
    uint32_t expected=d->opcode==FX_PTP_FETCH?32u:d->opcode==FX_PTP_CONTROL?28u:12u;
    if(fx_ptp_get32(q)!=expected || u16(q+4)!=1u || u16(q+6)!=d->opcode)
        return response(q,0x201du,mode);
    return 1u;
}
static uint32_t execute(const FxNativePtpDescriptor *d,const uint8_t *q) {
    if(!preflight(d,q)) return 0u;
    FxPtp *p=fx_native_ptp_profile.state();
    /* No menu owner exists before installation. Report a closed endpoint,
     * not transport contention; do not touch the native payload in that case. */
    if(!p)return response(q,0x2005u,d->direction==1u?2u:0u);
    uint8_t *payload=fx_native_ptp_profile.payload();
    uint32_t capacity=fx_native_ptp_profile.capacity();
    if(!payload || capacity<FX_PTP_FRAME) return response(q,0x2002u,d->direction==1u?2u:0u);
    uint32_t r,n=0;
    if(d->opcode==FX_PTP_SUBMIT) {
        const uint8_t *incoming=fx_native_ptp_profile.incoming();
        if(!incoming) return response(q,0x2007u,0u);
        uint32_t total=fx_ptp_get32(incoming);
        if(total<12u || total>12u+FX_PTP_FRAME || u16(incoming+4)!=2u ||
           u16(incoming+6)!=d->opcode || fx_ptp_get32(incoming+8)!=fx_ptp_get32(q+8))
            return response(q,0x201du,0u);
        r=fx_ptp_submit(p,payload,total-12u);
        return response(q,error_code(r),0u);
    }
    if(d->opcode==FX_PTP_HELLO) { r=fx_ptp_hello(p,payload);n=64u; }
    else if(d->opcode==FX_PTP_CONTROL) {
        r=fx_ptp_control(p,fx_ptp_get64(q+12),fx_ptp_get32(q+20),fx_ptp_get32(q+24),payload);
        n=FX_PTP_CONTROL_BYTES;
    } else {
        uint32_t maximum=fx_ptp_get32(q+28);
        r=fx_ptp_fetch(p,fx_ptp_get64(q+12),fx_ptp_get32(q+20),fx_ptp_get32(q+24),
            payload,maximum,&n);
    }
    if(r!=FX_PTP_OK) return response(q,error_code(r),2u);
    uint32_t sent=fx_native_ptp_profile.send
        (q,payload,n,1000u);
    return response(q,(sent&1u)?0x2001u:0x2007u,(sent&1u)?0u:1u);
}
static const FxNativePtpDescriptor rows[]={
    {FX_PTP_HELLO,1u,0,0,0,0,preflight,execute,0},
    {FX_PTP_SUBMIT,2u,0,0,0,buffer,preflight,execute,0},
    {FX_PTP_FETCH,1u,0,0,0,0,preflight,execute,0},
    {FX_PTP_CONTROL,1u,0,0,0,0,preflight,execute,0}
};
const FxNativePtpDescriptor *fx_native_ptp_lookup(uint32_t opcode) {
    /* Stable descriptor identity even if UI closes the endpoint between the
     * original preflight and execute phases. A NULL here would crash stock
     * execute dispatch. The portable transport owns local enable state. */
    if(opcode>=FX_PTP_HELLO && opcode<=FX_PTP_CONTROL) return &rows[opcode-FX_PTP_HELLO];
    return fx_native_ptp_profile.original_lookup(opcode);
}
void fx_native_ptp_session(uint32_t session) {
    if(*fx_native_ptp_profile.session!=session) {
        if(fx_native_ptp_profile.disconnect)fx_native_ptp_profile.disconnect();
        fx_ptp_disconnect(fx_native_ptp_profile.state());
    }
    fx_native_ptp_profile.original_session(session);
}
uint32_t fx_native_ptp_operations(uint8_t *out,uint32_t capacity,const uint8_t *operations) {
    if(operations==fx_native_ptp_profile.operations && fx_native_ptp_profile.active()) {
        FxPtp *ptp=fx_native_ptp_profile.state();
        if(ptp && atomic_load_explicit(&ptp->enabled,memory_order_acquire)) {
            uint32_t bytes=fx_ptp_extend_operations(out,capacity,operations,
                fx_native_ptp_profile.operations_capacity);
            if(bytes)return bytes;
        }
    }
    return fx_native_ptp_profile.original_operations(out,capacity,operations);
}
