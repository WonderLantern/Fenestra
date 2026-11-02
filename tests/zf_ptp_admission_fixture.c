#define _XOPEN_SOURCE 700
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "ptp_admission.h"

/* Host substitutes only retained-owner lookup/platform identity. Transport,
 * generation, lifecycle, backend and admission code are the actual sources. */
static FxPtp endpoint;
static FxZfPtpAuthorization authorization;
static FxZfPtpAdmission admission;
static FxZfLifecycle life;
static int available=1;
static const FxPlatform target={.abi=FX_PLATFORM_ABI,.identity={1},.module_identity={2}};
static const uint8_t application[32]={1},module_target[32]={2},build[32]={3},wrong[32]={4};
static const FxPtpIdentity identity={301u,FX_ZF_PTP_CAPS,FX_ZF_PTP_FLAGS,
    "Nikon Zf","C 3.01",application,module_target,build};
FxPtp *fx_zf_ptp_owner(void){return available?&endpoint:0;}
FxZfPtpAuthorization *fx_zf_ptp_authorization(void){return available?&authorization:0;}
FxZfPtpAdmission *fx_zf_ptp_admission(void){return available?&admission:0;}
FxZfLifecycle *fx_zf_runtime_lifecycle(void){return available?&life:0;}
const FxPlatform *fx_platform_target(void){return &target;}
static uint32_t rtos_calls,linux_calls,linux_ticks;
uint32_t fx_rtos_debug(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    assert(bytes==2u && !memcmp(in,"RT",2u) && *n>=2u);++rtos_calls;
    memcpy(out,"rt",2u);*n=2u;return FX_PTP_OK;
}
uint32_t fx_linux_request(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    assert(bytes==2u && !memcmp(in,"LX",2u) && *n>=2u);++linux_calls;
    memcpy(out,"lx",2u);*n=2u;return FX_PTP_OK;
}
void fx_linux_tick(uint64_t session,uint32_t generation,int enabled) {
    (void)session;(void)generation;(void)enabled;++linux_ticks;
}
static uint64_t hello(void) {
    uint8_t out[64];assert(fx_ptp_hello(&endpoint,out)==FX_PTP_OK);
    return fx_ptp_get64(out+8);
}
static void submit(uint64_t session,uint32_t id,uint16_t op,const void *data,uint32_t n) {
    uint8_t request[FX_PTP_FRAME]={0};memcpy(request,"FXP2",4u);
    request[4]=FX_PTP_VERSION;request[6]=(uint8_t)op;request[7]=(uint8_t)(op>>8);
    fx_ptp_put64(request+8,session);fx_ptp_put32(request+16,id);
    fx_ptp_put32(request+20,n);if(n)memcpy(request+FX_PTP_HEADER,data,n);
    fx_ptp_put32(request+28,fx_ptp_crc(request,FX_PTP_HEADER+n));
    assert(fx_ptp_submit(&endpoint,request,FX_PTP_HEADER+n)==FX_PTP_OK);
}
static void test_identity_and_consent(void) {
    fx_ptp_init(&endpoint);
    assert(!fx_zf_ptp_user_enabled() && !fx_zf_ptp_policy_permits());
    assert(!fx_zf_ptp_enter());
    assert(fx_zf_ptp_set_user_enabled(1)==FX_STATE);
    FxPtpIdentity bad=identity;bad.application=wrong;
    assert(fx_zf_ptp_bind(&bad)==FX_INTEGRITY);
    bad=identity;bad.module_target=wrong;assert(fx_zf_ptp_bind(&bad)==FX_INTEGRITY);
    bad=identity;bad.model="Nikon Z9";assert(fx_zf_ptp_bind(&bad)==FX_INTEGRITY);
    bad=identity;bad.revision="C 5.32";assert(fx_zf_ptp_bind(&bad)==FX_INTEGRITY);
    bad=identity;bad.firmware=532u;assert(fx_zf_ptp_bind(&bad)==FX_INTEGRITY);
    bad=identity;bad.flags|=2u;assert(fx_zf_ptp_bind(&bad)==FX_INTEGRITY);
    bad=identity;bad.capabilities|=1u<<(FX_PTP_REGIONS-1u);
    assert(fx_zf_ptp_bind(&bad)==FX_INTEGRITY);
    assert(fx_zf_ptp_bind(&identity)==FX_OK);
    assert(fx_zf_ptp_bind(&identity)==FX_STATE); /* No resetting active storage. */
    assert(!atomic_load(&endpoint.enabled));
    assert(fx_zf_ptp_set_activated(1)==FX_STATE); /* Not started yet. */
    fx_zf_lifecycle_record(&life,20u);
    assert(fx_zf_ptp_set_activated(1)==FX_OK && fx_zf_ptp_policy_permits());
    assert(!fx_zf_ptp_user_enabled() && !atomic_load(&endpoint.enabled));
    assert(fx_zf_ptp_set_user_enabled(2)==FX_ARGUMENT);
    assert(fx_zf_ptp_set_user_enabled(1)==FX_OK);
    assert(fx_zf_ptp_enter());fx_zf_ptp_leave();
    assert(fx_zf_ptp_set_user_enabled(1)==FX_OK && atomic_load(&endpoint.generation)==2u);
    puts("PASS own identity binding; explicit activation and consent; no automatic enable");
}
static void test_real_jobs_and_revocation(void) {
    uint64_t session=hello();uint8_t out[FX_PTP_FRAME];uint32_t n=0;
    submit(session,1u,FX_PTP_ECHO,"Zf",2u);
    assert(fx_zf_ptp_poll()==FX_PTP_OK);
    assert(fx_ptp_fetch(&endpoint,session,1u,0u,out,sizeof(out),&n)==FX_PTP_OK);
    assert(n==FX_PTP_HEADER+2u && !memcmp(out+FX_PTP_HEADER,"Zf",2u));
    submit(session,2u,FX_PTP_IDENTITY,0,0u);assert(fx_zf_ptp_poll()==FX_PTP_OK);
    assert(fx_ptp_fetch(&endpoint,session,2u,0u,out,sizeof(out),&n)==FX_PTP_OK);
    assert(n==FX_PTP_HEADER+FX_PTP_IDENTITY_BYTES);
    assert(!memcmp(out+FX_PTP_HEADER+160u,"Nikon Zf",8u));
    submit(session,3u,FX_PTP_STATUS,0,0u);assert(fx_zf_ptp_poll()==FX_PTP_OK);
    assert(fx_ptp_fetch(&endpoint,session,3u,0u,out,sizeof(out),&n)==FX_PTP_OK);
    assert(fx_ptp_get32(out+FX_PTP_HEADER)==301u);
    assert(fx_ptp_get32(out+FX_PTP_HEADER+8u)==FX_ZF_PTP_CAPS);
    submit(session,4u,FX_PTP_RTOS_MONITOR,"RT",2u);assert(fx_zf_ptp_poll()==FX_PTP_OK);
    assert(fx_ptp_fetch(&endpoint,session,4u,0u,out,sizeof(out),&n)==FX_PTP_OK);
    assert(n==FX_PTP_HEADER+2u && !memcmp(out+FX_PTP_HEADER,"rt",2u) && rtos_calls==1u);
    submit(session,5u,FX_PTP_LINUX_BRIDGE,"LX",2u);assert(fx_zf_ptp_poll()==FX_PTP_OK);
    assert(fx_ptp_fetch(&endpoint,session,5u,0u,out,sizeof(out),&n)==FX_PTP_OK);
    assert(n==FX_PTP_HEADER+2u && !memcmp(out+FX_PTP_HEADER,"lx",2u) && linux_calls==1u && linux_ticks);
    submit(session,6u,FX_PTP_ECHO,"not executed",12u);
    uint32_t executions=endpoint.executions,generation=atomic_load(&endpoint.generation);
    atomic_store(&authorization.request,123u);atomic_store(&authorization.generation,generation);
    assert(fx_zf_ptp_set_user_enabled(0)==FX_OK);
    assert(atomic_load(&endpoint.generation)==generation+1u);
    assert(!atomic_load(&authorization.request) && atomic_load(&authorization.generation)==UINT32_MAX);
    assert(fx_zf_ptp_poll()==FX_PTP_CLOSED && endpoint.executions==executions);
    assert(fx_zf_ptp_set_user_enabled(1)==FX_OK);
    assert(fx_ptp_fetch(&endpoint,session,6u,0u,out,sizeof(out),&n)==FX_PTP_STALE);
    assert(fx_zf_ptp_set_activated(0)==FX_OK && !fx_zf_ptp_user_enabled());
    assert(fx_zf_ptp_set_activated(1)==FX_OK && !atomic_load(&endpoint.enabled));
    assert(!fx_zf_ptp_enter());
    puts("PASS STATUS/ECHO/IDENTITY; RTOS/Linux backend routing; queued job and token revocation; old session stale");
}
static pthread_barrier_t entered,finish;
static void barrier(pthread_barrier_t *b) {
    int r=pthread_barrier_wait(b);assert(!r || r==PTHREAD_BARRIER_SERIAL_THREAD);
}
static void *native_callback(void *ignored) {
    (void)ignored;assert(fx_zf_ptp_enter());barrier(&entered);barrier(&finish);
    fx_zf_ptp_leave();return 0;
}
static void test_exclusion_stop_and_exhaustion(void) {
    assert(fx_zf_ptp_set_user_enabled(1)==FX_OK);
    assert(!pthread_barrier_init(&entered,0,2u) && !pthread_barrier_init(&finish,0,2u));
    pthread_t thread;assert(!pthread_create(&thread,0,native_callback,0));barrier(&entered);
    uint32_t g=atomic_load(&endpoint.generation);
    assert(fx_zf_ptp_set_user_enabled(0)==FX_BUSY);
    assert(!fx_zf_ptp_enter());assert(atomic_load(&endpoint.generation)==g);
    barrier(&finish);assert(!pthread_join(thread,0));
    assert(fx_zf_ptp_set_user_enabled(0)==FX_OK && atomic_load(&endpoint.generation)==g+1u);
    assert(!pthread_barrier_destroy(&entered) && !pthread_barrier_destroy(&finish));
    assert(fx_zf_ptp_set_user_enabled(1)==FX_OK);
    fx_zf_lifecycle_record(&life,8u);
    assert(!fx_zf_ptp_policy_permits() && fx_zf_ptp_poll()==FX_PTP_CLOSED);
    assert(!atomic_load(&endpoint.enabled));
    assert(fx_zf_ptp_set_activated(1)==FX_STATE && fx_zf_ptp_set_user_enabled(1)==FX_STATE);
    /* Synthetic terminal counter fixture: wraparound must never reopen. */
    atomic_store(&life.word,FX_ZF_LIFE_STARTED);
    assert(fx_zf_ptp_set_activated(1)==FX_OK);
    atomic_store(&endpoint.generation,UINT32_MAX-1u);
    assert(fx_zf_ptp_set_user_enabled(1)==FX_STATE);
    assert(atomic_load(&endpoint.generation)==UINT32_MAX && !atomic_load(&endpoint.enabled));
    assert(fx_zf_ptp_set_user_enabled(1)==FX_STATE && !fx_zf_ptp_user_enabled());
    available=0;assert(fx_zf_ptp_bind(&identity)==FX_STATE);
    assert(!fx_zf_ptp_enter() && !fx_zf_ptp_policy_permits() && !fx_zf_ptp_user_enabled());
    puts("PASS concurrent callback exclusion; close/retry; terminal STOP; generation saturation; missing owner");
}
int main(void) {
    test_identity_and_consent();test_real_jobs_and_revocation();test_exclusion_stop_and_exhaustion();return 0;
}
