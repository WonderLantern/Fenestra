#if !defined(FX_ZF_EXPERIMENTAL_PTP_ADMISSION) || FX_ZF_EXPERIMENTAL_PTP_ADMISSION != 1
#error "Zf PTP admission is experimental; local UI and native hooks require integration"
#endif
#include "ptp_admission.h"
#include "fenestra_memory.h"
#include "fenestra_platform_native.h"
#include "fenestra_native_rtos.h"
#include "fenestra_native_linux.h"

static int take(FxZfPtpAdmission *g) {
    uint32_t zero=0;
    return g && atomic_compare_exchange_strong_explicit(&g->locked,&zero,1u,
        memory_order_acquire,memory_order_relaxed);
}
static void release(FxZfPtpAdmission *g) {
    atomic_store_explicit(&g->locked,0u,memory_order_release);
}
static int text_equal(const char *a,const char *b) {
    while(*b){if(*a++!=*b++)return 0;}return *a==0;
}
static void revoke(FxPtp *p) {
    FxZfPtpAuthorization *a=fx_zf_ptp_authorization();
    if(a) {
        atomic_store_explicit(&a->request,0u,memory_order_release);
        atomic_store_explicit(&a->generation,UINT32_MAX,memory_order_release);
    }
    if(p && atomic_load_explicit(&p->enabled,memory_order_acquire))fx_ptp_enable(p,0);
}
int fx_zf_ptp_user_enabled(void) {
    FxZfPtpAdmission *g=fx_zf_ptp_admission();
    return g && atomic_load_explicit(&g->consent,memory_order_acquire)!=0u;
}
int fx_zf_ptp_policy_permits(void) {
    FxZfPtpAdmission *g=fx_zf_ptp_admission();
    FxZfLifecycle *life=fx_zf_runtime_lifecycle();
    if(!g || !life || !atomic_load_explicit(&g->bound,memory_order_acquire) ||
       !atomic_load_explicit(&g->activated,memory_order_acquire))return 0;
    uint32_t word=atomic_load_explicit(&life->word,memory_order_acquire);
    /* Diagnostic transport does not require a mounted card. STOP is terminal.
     * This is Fenestra policy, not an assertion about Nikon maintenance gates. */
    return (word&(FX_ZF_LIFE_STARTED|FX_ZF_LIFE_STOP))==FX_ZF_LIFE_STARTED;
}
static int ready(FxPtp *p) {
    return p && fx_zf_ptp_policy_permits() && fx_zf_ptp_user_enabled() &&
        atomic_load_explicit(&p->enabled,memory_order_acquire)!=0u;
}
int fx_zf_ptp_enter(void) {
    FxZfPtpAdmission *g=fx_zf_ptp_admission();
    if(!take(g))return 0;
    FxPtp *p=fx_zf_ptp_owner();
    if(!ready(p)){revoke(p);release(g);return 0;}
    return 1;
}
void fx_zf_ptp_leave(void) {
    FxZfPtpAdmission *g=fx_zf_ptp_admission();
    /* Retained owner cannot disappear while native callbacks exist. */
    if(g)release(g);
}
FxResult fx_zf_ptp_bind(const FxPtpIdentity *id) {
    FxZfPtpAdmission *g=fx_zf_ptp_admission();FxPtp *p=fx_zf_ptp_owner();
    const FxPlatform *target=fx_platform_target();
    if(!g || !p || !fx_zf_ptp_authorization() || !target)return FX_STATE;
    if(!id || id->firmware!=301u || id->capabilities!=FX_ZF_PTP_CAPS ||
       id->flags!=FX_ZF_PTP_FLAGS || !id->model || !id->revision ||
       !text_equal(id->model,"Nikon Zf") || !text_equal(id->revision,"C 3.01") ||
       !id->application || !id->module_target || !id->build ||
       memcmp(id->application,target->identity,32u) ||
       memcmp(id->module_target,target->module_identity,32u))return FX_INTEGRITY;
    if(!take(g))return FX_BUSY;
    FxResult result=FX_STATE;
    if(!atomic_load_explicit(&g->bound,memory_order_acquire) &&
       (!p->identity || p->identity==id) &&
       !atomic_load_explicit(&p->enabled,memory_order_acquire) &&
       atomic_load_explicit(&p->generation,memory_order_acquire)==1u &&
       !atomic_load_explicit(&p->locked,memory_order_acquire) && p->job==FX_PTP_IDLE) {
        p->identity=id;
        atomic_store_explicit(&g->bound,1u,memory_order_release);
        result=FX_OK;
    }
    release(g);return result;
}
static FxResult set_gate(int enable,int consent) {
    FxZfPtpAdmission *g=fx_zf_ptp_admission();FxPtp *p=fx_zf_ptp_owner();
    if(!g || !p)return FX_STATE;
    if(enable!=0 && enable!=1)return FX_ARGUMENT;
    if(!take(g))return FX_BUSY;
    FxResult result=FX_OK;
    FxZfLifecycle *life=fx_zf_runtime_lifecycle();
    uint32_t word=life?atomic_load_explicit(&life->word,memory_order_acquire):0u;
    if(enable && (!atomic_load_explicit(&g->bound,memory_order_acquire) ||
                  (word&(FX_ZF_LIFE_STARTED|FX_ZF_LIFE_STOP))!=FX_ZF_LIFE_STARTED ||
                  (consent && !fx_zf_ptp_policy_permits())))result=FX_STATE;
    else {
        if(consent)atomic_store_explicit(&g->consent,(uint32_t)enable,memory_order_release);
        else atomic_store_explicit(&g->activated,(uint32_t)enable,memory_order_release);
        if(!enable) {
            /* Core deactivation also forgets consent: no automatic reopening. */
            atomic_store_explicit(&g->consent,0u,memory_order_release);
            revoke(p);
        } else if(consent && !atomic_load_explicit(&p->enabled,memory_order_acquire)) {
            fx_ptp_enable(p,1);
            if(!atomic_load_explicit(&p->enabled,memory_order_acquire)) {
                atomic_store_explicit(&g->consent,0u,memory_order_release);result=FX_STATE;
            }
        }
    }
    release(g);return result;
}
FxResult fx_zf_ptp_set_activated(int enable){return set_gate(enable,0);}
FxResult fx_zf_ptp_set_user_enabled(int enable){return set_gate(enable,1);}
static uint32_t backend(void *context,uint16_t op,const uint8_t *in,uint32_t bytes,
                        uint8_t *out,uint32_t *count) {
    FxPtp *p=context;
    if(op==FX_PTP_RTOS_MONITOR)return fx_rtos_debug(in,bytes,out,count);
    if(op==FX_PTP_LINUX_BRIDGE)return fx_linux_request(in,bytes,out,count);
    if(op==FX_PTP_ECHO) {
        if(*count<bytes)return FX_PTP_ARGUMENT;
        memcpy(out,in,bytes);*count=bytes;return FX_PTP_OK;
    }
    if(op==FX_PTP_STATUS && !bytes) {
        if(*count<32u)return FX_PTP_ARGUMENT;
        memset(out,0,32u);fx_ptp_put32(out,301u);
        fx_ptp_put32(out+4,(uint32_t)ready(p));fx_ptp_put32(out+8,FX_ZF_PTP_CAPS);
        *count=32u;return FX_PTP_OK;
    }
    return FX_PTP_UNSUPPORTED;
}
uint32_t fx_zf_ptp_poll(void) {
    FxPtp *p=fx_zf_ptp_owner();
    if(p)fx_linux_tick(atomic_load_explicit(&p->backend_session,memory_order_acquire),
        atomic_load_explicit(&p->generation,memory_order_acquire),ready(p));
    if(!fx_zf_ptp_enter())return FX_PTP_CLOSED;
    fx_zf_ptp_leave();
    /* Shared transport retains RUNNING storage and drops completions whose
     * generation was revoked. Do not hold admission exclusion over a worker. */
    return fx_ptp_poll(p,backend,p);
}
