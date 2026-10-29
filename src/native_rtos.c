/* Privileged local USB debug. Native commands run on a dedicated Nikon task,
 * never on the USB/core worker. Explicitly cancelled tasks are retained while
 * native code may still hold locks. This is not a native-fault recovery sandbox. */
#include "fenestra_native_rtos.h"
#include "fenestra_ptp.h"
#include "fenestra_memory.h"
void fx_rtos_worker_step(FxRtos *s) {
    uint32_t expected=FX_RTOS_QUEUED;
    if(atomic_load_explicit(&s->closed,memory_order_acquire)) {
        (void)atomic_compare_exchange_strong(&s->phase,&expected,FX_RTOS_REVOKED);
    } else if(atomic_compare_exchange_strong_explicit(&s->phase,&expected,FX_RTOS_RUNNING,
                  memory_order_acq_rel,memory_order_acquire)) {
        /* Debug jobs have no session or deadline admission policy. */
        if(atomic_load_explicit(&s->phase,memory_order_acquire)!=FX_RTOS_RUNNING)return;
        if(s->kind==0u) {
            char *parameters=s->command;
            while(*parameters && *parameters!=' ' && *parameters!='\t')++parameters;
            if(*parameters)*parameters++=0;
            while(*parameters==' ' || *parameters=='\t')++parameters;
            fx_native_rtos_profile.command(s->command,parameters);
            s->result=0; /* parser has no command exit-code contract */
        } else {
            s->result=((uint64_t (*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))
                (uintptr_t)s->address)(s->args[0],s->args[1],s->args[2],s->args[3],
                                     s->args[4],s->args[5],s->args[6],s->args[7]);
        }
        /* Completion cannot erase explicit cancellation. Closed jobs
         * stay non-reusable until reboot, even if native code returns. */
        expected=FX_RTOS_RUNNING;
        (void)atomic_compare_exchange_strong_explicit(&s->phase,&expected,FX_RTOS_DONE,
            memory_order_release,memory_order_relaxed);
    }
}
static void worker(void *argument) {
    for(;;) {fx_rtos_worker_step(argument);(void)fx_native_rtos_profile.delay(10u);}
}
void fx_rtos_revoke(void) {
    FxRtos *s=fx_native_rtos_profile.state();if(!s)return;
    uint32_t phase=atomic_load_explicit(&s->phase,memory_order_acquire);
    if(phase==FX_RTOS_IDLE || phase==FX_RTOS_DONE)return;
    atomic_store_explicit(&s->closed,1u,memory_order_release);
    while(phase==FX_RTOS_QUEUED || phase==FX_RTOS_RUNNING) {
        if(atomic_compare_exchange_weak(&s->phase,&phase,FX_RTOS_REVOKED))break;
    }
}
int fx_rtos_quiesced(void) {
    FxRtos *s=fx_native_rtos_profile.state();if(!s)return 1;
    uint32_t phase=atomic_load_explicit(&s->phase,memory_order_acquire);
    return !atomic_load(&s->closed) && (phase==FX_RTOS_IDLE || phase==FX_RTOS_DONE);
}
uint32_t fx_rtos_vprintf(const char *format,const void *ap,uintptr_t caller_sp) {
    FxRtos *s=fx_native_rtos_profile.state();if(!s || !s->task)return 0;
    /* Stack ownership avoids capturing other native tasks or changing their
     * output selection. IRQ-stack output is not attributed to this command. */
    if(caller_sp<(uintptr_t)s->stack || caller_sp>=(uintptr_t)(s->stack+sizeof(s->stack)))return 0;
    uint32_t phase=atomic_load_explicit(&s->phase,memory_order_acquire);
    if(phase!=FX_RTOS_RUNNING && phase!=FX_RTOS_EXPIRED && phase!=FX_RTOS_REVOKED)return 0;
    char text[512];memset(text,0,sizeof(text));
    (void)fx_native_rtos_profile.format
        (text,sizeof(text),format,ap);
    uint32_t length=0;while(length<sizeof(text)-1u && text[length])++length;
    uint32_t unlocked=0;
    if(!atomic_compare_exchange_strong_explicit(&s->output_lock,&unlocked,1u,memory_order_acquire,memory_order_relaxed)) {
        atomic_fetch_add(&s->dropped,1u);return 1;
    }
    uint32_t used=atomic_load_explicit(&s->output_bytes,memory_order_relaxed);
    uint32_t copy=length;if(copy>FX_RTOS_OUTPUT-used)copy=FX_RTOS_OUTPUT-used;
    memcpy(s->output+used,text,copy);
    if(copy!=length || length==sizeof(text)-1u)atomic_fetch_add(&s->dropped,1u);
    atomic_store_explicit(&s->output_bytes,used+copy,memory_order_release);
    atomic_store_explicit(&s->output_lock,0u,memory_order_release);
    return 1;
}
uint32_t fx_rtos_request(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    FxRtos *s=fx_native_rtos_profile.state();if(!s || bytes<4u)return FX_PTP_ARGUMENT;
    uint32_t op=fx_ptp_get32(in);in+=4u;bytes-=4u;
    uint32_t phase=atomic_load_explicit(&s->phase,memory_order_acquire);
    if(op==4u) {
        if(bytes)return FX_PTP_ARGUMENT;
        memset(out,0,64u);memcpy(out,"FXS1",4u);fx_ptp_put32(out+4,1u);
        fx_ptp_put32(out+8,3u);fx_ptp_put32(out+12,phase);fx_ptp_put32(out+16,s->job);
        fx_ptp_put32(out+20,(uint32_t)s->task);
        /* result is read only after release/acquire completion. */
        if(phase==FX_RTOS_DONE)fx_ptp_put64(out+24,s->result);
        fx_ptp_put32(out+32,atomic_load_explicit(&s->output_bytes,memory_order_acquire));
        fx_ptp_put32(out+36,atomic_load(&s->dropped));fx_ptp_put64(out+40,s->session);
        fx_ptp_put32(out+48,s->generation);fx_ptp_put32(out+52,atomic_load(&s->closed));
        fx_ptp_put32(out+56,s->timeout_ms);fx_ptp_put32(out+60,s->kind);*n=64u;return FX_PTP_OK;
    }
    if(op==6u) {
        if(bytes!=12u || fx_ptp_get32(in)!=s->job || !s->job)return FX_PTP_ARGUMENT;
        uint32_t at=fx_ptp_get32(in+4),count=fx_ptp_get32(in+8);
        uint32_t used=atomic_load_explicit(&s->output_bytes,memory_order_acquire);
        if(!count || count>FX_PTP_PAYLOAD || at>used || count>used-at)return FX_PTP_ARGUMENT;
        memcpy(out,s->output+at,count);*n=count;return FX_PTP_OK;
    }
    if(op==7u) {
        if(bytes!=4u || fx_ptp_get32(in)!=s->job)return FX_PTP_ARGUMENT;
        if(phase!=FX_RTOS_DONE || atomic_load(&s->closed))return FX_PTP_BUSY;
        atomic_store_explicit(&s->phase,FX_RTOS_IDLE,memory_order_release);*n=0;return FX_PTP_OK;
    }
    if(op==8u) {
        if(bytes!=4u || !s->job || fx_ptp_get32(in)!=s->job)return FX_PTP_ARGUMENT;
        fx_rtos_revoke();*n=0;return FX_PTP_OK;
    }
    if(op!=5u)return FX_PTP_UNSUPPORTED;
    if(bytes<8u)return FX_PTP_ARGUMENT;
    uint32_t kind=fx_ptp_get32(in),timeout=fx_ptp_get32(in+4);in+=8u;bytes-=8u;
    if(kind>1u)return FX_PTP_ARGUMENT;
    if(phase!=FX_RTOS_IDLE || atomic_load(&s->closed))return FX_PTP_BUSY;
    if(kind==0u) {
        if(!bytes || bytes>=sizeof(s->command))return FX_PTP_ARGUMENT;
        memcpy(s->command,in,bytes);s->command[bytes]=0;
    } else {
        if(bytes!=72u)return FX_PTP_ARGUMENT;
        uint64_t address=fx_ptp_get64(in);
        s->address=address;
        for(uint32_t i=0;i<8u;++i)s->args[i]=fx_ptp_get64(in+8u+i*8u);
    }
    FxPtp *p=fx_native_rtos_profile.ptp();if(!p)return FX_PTP_CLOSED;
    s->session=atomic_load_explicit(&p->backend_session,memory_order_acquire);
    s->generation=atomic_load_explicit(&p->generation,memory_order_acquire);
    if(!s->task) {
        FxRtosNativeTask task={0,0,(uintptr_t)s,(uintptr_t)worker,28,0,sizeof(s->stack),(uintptr_t)s->stack,1,UINT32_MAX};
        s->task=fx_native_rtos_profile.create(&task);
        if(s->task<=0 || fx_native_rtos_profile.resume(s->task)!=0) {
            atomic_store(&s->closed,1u);return FX_PTP_UNSUPPORTED;
        }
    }
    s->kind=kind;s->timeout_ms=timeout;s->result=0;
    s->deadline=0; /* Timeout is metadata, never an automatic cancellation policy. */
    atomic_store(&s->output_bytes,0u);atomic_store(&s->dropped,0u);
    if(++s->job==0u)++s->job;
    atomic_store_explicit(&s->phase,FX_RTOS_QUEUED,memory_order_release);
    fx_ptp_put32(out,s->job);*n=4u;return FX_PTP_OK;
}
const uint32_t fx_native_rtos_metadata[]={0x46585254u,1u,sizeof(FxRtos),
    offsetof(FxRtos,phase),offsetof(FxRtos,closed),offsetof(FxRtos,task),
    offsetof(FxRtos,stack),offsetof(FxRtos,output_bytes),offsetof(FxRtos,output),
    offsetof(FxRtos,job),offsetof(FxRtos,result),offsetof(FxRtos,command)};
