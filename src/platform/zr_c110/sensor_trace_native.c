/* Native sensor-only observer. No general function-hook command, MMIO,
 * register write, root tooling or invocation of any sensor function exists. */
#include "sensor_trace_native.h"
#include "fenestra_c110_observation.h"
#include "fenestra_c110_rtos.h"
#include "fenestra_ptp.h"
#include "fenestra_memory.h"
#include "c110_debug_identity.h"
#ifdef FX_C110_EVENT_TRACE
#include "event_trace_native.h"
#endif
static uint64_t clock_read(void *p){(void)p;uint64_t n;__asm__ volatile("mrs %0,cntpct_el0":"=r"(n));return n;}
static int read_source(void *p,uint64_t a,uint32_t n,uint8_t *d,uint64_t *par) {
    (void)p;return fx_c110_rtos_read_normal(a,n,d,par);
}
static int admitted(void) {
    FxPtp *p=fx_c110_menu_ptp();return p && fx_c110_menu_ptp_active() && atomic_load_explicit(&p->enabled,memory_order_acquire);
}
static FxObservationIO current(void) {
    FxC110Observation *s=fx_c110_menu_observation();uint64_t hz;
    __asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
    FxObservationIO io={0,clock_read,read_source,hz,s?s->boot:0,fx_c110_observation_activation(),
        s?atomic_load_explicit(&s->disconnect_epoch,memory_order_acquire):0,{0}};
    memcpy(io.build,fx_c110_debug_identity,32u);return io;
}
uint64_t fx_c110_sensor_trace_enter(FxSensorTraceFrame *frame) {
    FxSensorTrace *s=fx_c110_menu_sensor_trace();
    uint64_t token=0;
    if(s && atomic_load_explicit(&s->accepting,memory_order_acquire)) {
        FxObservationIO io=current();token=fx_sensor_trace_enter(s,&io,frame,admitted());
    }
#ifdef FX_C110_EVENT_TRACE
    fx_c110_event_trace_sensor(frame,3u,token?s->id:0,token);
#endif
    return token;
}
void fx_c110_sensor_trace_leave(FxSensorTraceFrame *frame) {
    FxSensorTrace *s=fx_c110_menu_sensor_trace();
#ifdef FX_C110_EVENT_TRACE
    fx_c110_event_trace_sensor(frame,4u,frame->token && s?s->id:0,frame->token);
#endif
    if(!frame->token || !s)return;
    FxObservationIO io=current();fx_sensor_trace_leave(s,&io,frame);
}
void fx_c110_sensor_trace_cancel(uint32_t reason){fx_sensor_trace_cancel(fx_c110_menu_sensor_trace(),reason);}
void fx_c110_sensor_trace_step(const FxObservationIO *io,int allowed){fx_sensor_trace_step(fx_c110_menu_sensor_trace(),io,allowed);}
int fx_c110_sensor_trace_quiesced(void){return fx_sensor_trace_quiesced(fx_c110_menu_sensor_trace());}
uint32_t fx_c110_sensor_trace_request(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    *n=0;if(!admitted())return FX_PTP_CLOSED;
    if(bytes<4u)return FX_PTP_ARGUMENT;
    FxObservationIO io=current();uint32_t op=fx_ptp_get32(in);
    if(op==FX_ST_DESCRIBE) {
        if(bytes!=4u)return FX_PTP_ARGUMENT;
        memset(out,0,224u);memcpy(out,"FXTD",4u);fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,224u);
        fx_ptp_put32(out+12,7u); /* software submit, software return, same-call token ONLY */
        fx_ptp_put32(out+16,FX_SENSOR_TRACE_RECORD);fx_ptp_put32(out+20,FX_SENSOR_TRACE_BYTES);
        fx_ptp_put32(out+24,30000u);fx_ptp_put32(out+28,FX_SENSOR_TRACE_PAIRS);
        fx_ptp_put32(out+32,64u);fx_ptp_put32(out+36,16u);fx_ptp_put32(out+40,1u);
        fx_ptp_put64(out+48,io.boot);fx_ptp_put64(out+56,io.activation);
        memcpy(out+64,fx_c110_debug_base,32u);memcpy(out+96,io.build,32u);memcpy(out+128,fx_c110_debug_package,32u);
        fx_ptp_put32(out+160,8u);fx_ptp_put32(out+164,1u);
        fx_ptp_put64(out+168,UINT64_C(0x413522a8));fx_ptp_put64(out+176,UINT64_C(0x413522ac));
        fx_ptp_put64(out+184,UINT64_C(0xa057af9ec));fx_ptp_put32(out+192,1u);
        *n=224u;return FX_PTP_OK;
    }
    FxSensorTrace *s=fx_c110_menu_sensor_trace();
    if(!s && op==FX_ST_ARM) {
        if(bytes!=36u || !fx_ptp_get32(in+20) || fx_ptp_get32(in+20)>30000u ||
           !fx_ptp_get32(in+24) || fx_ptp_get32(in+24)>FX_SENSOR_TRACE_PAIRS ||
           (fx_ptp_get32(in+28)&~FX_OBS_ALLOW_DISCONNECT) || fx_ptp_get32(in+32))return FX_PTP_ARGUMENT;
        uint8_t any=0;for(uint32_t i=4u;i<20u;++i)any|=in[i];if(!any)return FX_PTP_ARGUMENT;
        s=fx_c110_menu_sensor_trace_create();
    }
    if(!s)return op==FX_ST_ARM?FX_PTP_UNSUPPORTED:FX_PTP_STALE;
    return fx_sensor_trace_request(s,&io,in,bytes,out,n);
}
const uint32_t fx_c110_sensor_trace_metadata[]={0x46585444u,1u,sizeof(FxSensorTrace),
    offsetof(FxSensorTrace,phase),offsetof(FxSensorTrace,pairs),offsetof(FxSensorTrace,inflight),
    offsetof(FxSensorTrace,data),FX_SENSOR_TRACE_BYTES,FX_SENSOR_TRACE_RECORD,
    offsetof(FxSensorTrace,accepting),offsetof(FxSensorTrace,callbacks)};
