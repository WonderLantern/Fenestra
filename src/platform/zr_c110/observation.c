/* Native recorder task, created on explicit ARM only. The Nikon scheduler
 * drives it independently of USB; it never borrows a stock capture task. */
#include "fenestra_c110_observation.h"
#include "fenestra_c110_rtos.h"
#include "fenestra_ptp.h"
#include "fenestra_memory.h"
#include "c110_debug_identity.h"
#include <stddef.h>
#ifdef FX_C110_SENSOR_TRACE
#include "sensor_trace_native.h"
#endif
#ifdef FX_C110_EVENT_TRACE
#include "event_trace_native.h"
#include "frame_sample_native.h"
#endif
#define FN(a,t) ((t)(uintptr_t)(a))
typedef struct {
    uint32_t attributes,reserved;uintptr_t context,entry;
    uint32_t priority,reserved2;uint64_t stack_bytes;uintptr_t stack;
    uint32_t processor,reserved3;
} Task;
_Static_assert(sizeof(Task)==56,"C1.10 recorder task record");
static uint64_t clock(void *context) {
    (void)context;uint64_t value;__asm__ volatile("mrs %0,cntpct_el0":"=r"(value));return value;
}
static int read_normal(void *context,uint64_t address,uint32_t bytes,uint8_t *out,uint64_t *par) {
    (void)context;return fx_c110_rtos_read_normal(address,bytes,out,par);
}
static FxObservationIO io(FxC110Observation *s) {
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    FxObservationIO result={s,clock,read_normal,frequency,s->boot,fx_c110_observation_activation(),
        atomic_load_explicit(&s->disconnect_epoch,memory_order_acquire),{0}};
    memcpy(result.build,fx_c110_debug_identity,32u);return result;
}
static int admitted(void) {
    FxPtp *p=fx_c110_menu_ptp();
    return p && fx_c110_menu_ptp_active() && atomic_load_explicit(&p->enabled,memory_order_acquire);
}
static void worker(void *argument) {
    FxC110Observation *s=argument;
    for(;;) {
        FxObservationIO current=io(s);
        fx_observation_step(&s->recorder,&current,admitted());
#ifdef FX_C110_SENSOR_TRACE
        fx_c110_sensor_trace_step(&current,admitted());
#endif
#ifdef FX_C110_EVENT_TRACE
        fx_c110_event_trace_step(&current,admitted());
        fx_c110_frame_step(&current,admitted());
#endif
        (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(10u);
    }
}
void fx_c110_observation_init(FxC110Observation *s) {
    fx_observation_init(&s->recorder);s->boot=clock(0);if(!s->boot)s->boot=1;
}
void fx_c110_observation_cancel(uint32_t reason) {
    FxC110Observation *s=fx_c110_menu_observation();if(s)fx_observation_cancel(&s->recorder,reason);
#ifdef FX_C110_SENSOR_TRACE
    fx_c110_sensor_trace_cancel(reason);
#endif
#ifdef FX_C110_EVENT_TRACE
    fx_c110_event_trace_cancel(reason);
    fx_c110_frame_cancel(reason);
#endif
}
void fx_c110_observation_disconnect(void) {
    FxC110Observation *s=fx_c110_menu_observation();if(!s)return;
    /* This is NOT the local Debug Off event. Only the explicitly armed read
     * recorder has an exemption; shell/native-call revocation is unchanged. */
    uint64_t old=atomic_load_explicit(&s->disconnect_epoch,memory_order_relaxed);
    while(old!=UINT64_MAX && !atomic_compare_exchange_weak_explicit(&s->disconnect_epoch,&old,old+1u,
        memory_order_release,memory_order_relaxed)) {}
}
int fx_c110_observation_quiesced(void) {
    FxC110Observation *s=fx_c110_menu_observation();return (!s || fx_observation_quiesced(&s->recorder))
#ifdef FX_C110_SENSOR_TRACE
        && fx_c110_sensor_trace_quiesced()
#endif
#ifdef FX_C110_EVENT_TRACE
        && fx_c110_event_trace_quiesced()
        && fx_c110_frame_quiesced()
#endif
        ;
}
static int start(FxC110Observation *s) {
    if(atomic_load_explicit(&s->task,memory_order_acquire)>0)return 1;
    uint32_t zero=0;if(!atomic_compare_exchange_strong(&s->created,&zero,1u))return 0;
    Task task={0,0,(uintptr_t)s,(uintptr_t)worker,28,0,sizeof(s->stack),(uintptr_t)s->stack,1,UINT32_MAX};
    int32_t id=FN(UINT64_C(0x4018507c),int32_t (*)(const Task *))(&task);
    if(id<=0)return 0;
    atomic_store_explicit(&s->task,id,memory_order_release);
    if(FN(UINT64_C(0x401855f8),int32_t (*)(int32_t))(id)<0){atomic_store(&s->task,-1);return 0;}
    return 1;
}
uint32_t fx_c110_observation_request(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    *n=0;FxC110Observation *s=fx_c110_menu_observation();
    if(!s)return FX_PTP_CLOSED;
    if(bytes<4u)return FX_PTP_ARGUMENT;
    uint32_t operation=fx_ptp_get32(in);FxObservationIO current=io(s);
    if(operation==FX_OBS_DESCRIBE) {
        if(bytes!=4u)return FX_PTP_ARGUMENT;
        memset(out,0,224u);memcpy(out,"FXOV",4u);fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,224u);
        fx_ptp_put32(out+12,15u); /* normal read, video-pool read, context, offline */
#ifdef FX_C110_SENSOR_TRACE
        fx_ptp_put32(out+12,31u); /* passive sensor software-submit; REC/frame/ROI remain unavailable */
#endif
#ifdef FX_C110_EVENT_TRACE
        fx_ptp_put32(out+12,63u); /* fixed metadata and original accepted REC callbacks; no frame/lease/ROI claim */
#endif
        fx_ptp_put32(out+16,1008u);fx_ptp_put32(out+20,FX_OBS_BYTES);fx_ptp_put32(out+24,30000u);
        fx_ptp_put32(out+28,10u);fx_ptp_put32(out+32,FX_OBS_RECORDS);fx_ptp_put32(out+36,FX_OBS_JOBS);
        fx_ptp_put32(out+40,1u);fx_ptp_put32(out+44,FX_OBS_RECORD_BYTES);
        fx_ptp_put64(out+48,current.boot);fx_ptp_put64(out+56,current.activation);
        memcpy(out+64,fx_c110_debug_base,32u);memcpy(out+96,current.build,32u);
        memcpy(out+128,fx_c110_debug_package,32u);fx_ptp_put32(out+160,1u); /* read-only */
        fx_ptp_put32(out+164,1u); /* one retained capture, not text stdout */
        fx_ptp_put64(out+168,UINT64_C(0x8af5d9c00));fx_ptp_put64(out+176,UINT64_C(0x8f025fc00));
        *n=224u;return FX_PTP_OK;
    }
#ifdef FX_C110_EVENT_TRACE
    if(operation==FX_OBS_FRAME) {
        uint32_t result=fx_c110_frame_request(in+4u,bytes-4u,out,n);
        if(result==FX_PTP_OK && bytes>=8u && fx_ptp_get32(in+4u)==1u && !start(s)) {
            fx_c110_frame_cancel(FX_OBS_SOURCE_ERROR);fx_c110_frame_step(&current,0);*n=0;return FX_PTP_UNSUPPORTED;
        }
        return result;
    }
    if(operation==10u) {
        uint32_t result=fx_c110_event_trace_request(in+4u,bytes-4u,out,n);
        if(result==FX_PTP_OK && bytes>=8u && fx_ptp_get32(in+4u)==1u && !start(s)) {
            fx_c110_event_trace_cancel(FX_OBS_SOURCE_ERROR);fx_c110_event_trace_step(&current,0);
            *n=0;return FX_PTP_UNSUPPORTED;
        }
        return result;
    }
#endif
#ifdef FX_C110_SENSOR_TRACE
    if(operation==FX_OBS_TRACE) {
        uint32_t result=fx_c110_sensor_trace_request(in+4u,bytes-4u,out,n);
        if(result==FX_PTP_OK && bytes>=8u && fx_ptp_get32(in+4u)==FX_ST_ARM && !start(s)) {
            fx_c110_sensor_trace_cancel(FX_OBS_SOURCE_ERROR);fx_c110_sensor_trace_step(&current,0);
            *n=0;return FX_PTP_UNSUPPORTED;
        }
        return result;
    }
#endif
    uint32_t result=fx_observation_request(&s->recorder,&current,in,bytes,out,n);
    if(result==FX_PTP_OK && (operation==FX_OBS_ARM || operation==FX_OBS_SNAPSHOT) && !start(s)) {
        s->recorder.error=FX_PTP_UNSUPPORTED;
        fx_observation_cancel(&s->recorder,FX_OBS_SOURCE_ERROR);
        fx_observation_step(&s->recorder,&current,0);*n=0;return FX_PTP_UNSUPPORTED;
    }
    return result;
}
const uint32_t fx_c110_observation_metadata[]={0x46584f56u,1u,sizeof(FxC110Observation),
    offsetof(FxC110Observation,boot),offsetof(FxC110Observation,task),
    offsetof(FxC110Observation,stack),sizeof(((FxC110Observation *)0)->stack),
    offsetof(FxC110Observation,disconnect_epoch),offsetof(FxObservation,phase),
    offsetof(FxObservation,flags),offsetof(FxObservation,records),offsetof(FxObservation,used),
    offsetof(FxObservation,jobs),offsetof(FxObservation,data),offsetof(FxObservation,deadline),
    offsetof(FxObservation,lock),offsetof(FxC110Observation,created)};
