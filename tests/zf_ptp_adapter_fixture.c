#include "ptp.h"
#include "fenestra_platform.h"
#include <stdint.h>
#include <stdio.h>

#ifndef FX_ZF_C301_PTP_EXPERIMENT
#define FX_ZF_C301_PTP_EXPERIMENT 0
#endif

static FxPtp owner;
static const FxPlatform target={.abi=FX_PLATFORM_ABI};
const FxPlatform *fx_platform_target(void) { return &target; }
void fx_ptp_init(FxPtp *p) { *p=(FxPtp){0};atomic_init(&p->generation,1u); }
FxResult fx_zf_ptp_bind(const FxPtpIdentity *id) { return id?FX_OK:FX_ARGUMENT; }
static FxZfPtpAuthorization authorization;
static int authorization_available=1;
FxZfPtpAuthorization *fx_zf_ptp_authorization(void){return authorization_available?&authorization:0;}
static int enabled,policy;
static int old_lookup,old_session,old_ops,new_lookup,new_session,new_ops,buffer_calls,execute_calls;
static int change_generation_in_buffer;
static const FxNativePtpDescriptor stock={0};
FxPtp *fx_zf_ptp_owner(void) { return &owner; }
int fx_zf_ptp_user_enabled(void) { return enabled; }
int fx_zf_ptp_policy_permits(void) { return policy; }
/* This adapter-only fixture substitutes admission; real state/transport and
 * concurrent revocation are checked in zf_ptp_admission_fixture.c. */
static int callback_locked;
int fx_zf_ptp_enter(void) {
    if(callback_locked || !enabled || !policy || !authorization_available || !atomic_load(&owner.enabled))return 0;
    callback_locked=1;return 1;
}
void fx_zf_ptp_leave(void){callback_locked=0;}
void fx_ptp_disconnect(FxPtp *p) { (void)p; }
void fx_linux_revoke(void) {}
const FxNativePtpDescriptor *fx_zf_ptp_original_lookup(uint32_t op) { (void)op;old_lookup++;return &stock; }
void fx_zf_ptp_original_session(uint32_t session) { (void)session;old_session++; }
uint32_t fx_zf_ptp_original_operations(uint8_t *out,uint32_t cap,const uint8_t *ops) {
    (void)out;(void)cap;(void)ops;old_ops++;return 11;
}
static uint32_t fake_buffer(const FxNativePtpDescriptor *d,const uint8_t *q,uint8_t **out,uint32_t *bytes) {
    (void)d;(void)q;buffer_calls++;*out=(uint8_t *)&owner;*bytes=12;
    if(change_generation_in_buffer)atomic_fetch_add(&owner.generation,1u);
    return 1;
}
static uint32_t fake_execute(const FxNativePtpDescriptor *d,const uint8_t *q) {
    (void)d;(void)q;execute_calls++;return fx_native_ptp_profile.state()!=0;
}
static const FxNativePtpDescriptor extension={0,0,0,0,0,fake_buffer,0,fake_execute,0};
const FxNativePtpDescriptor *fx_native_ptp_lookup(uint32_t op) { (void)op;new_lookup++;return &extension; }
void fx_native_ptp_session(uint32_t session) { (void)session;new_session++; }
uint32_t fx_native_ptp_operations(uint8_t *out,uint32_t cap,const uint8_t *ops) {
    (void)out;(void)cap;(void)ops;new_ops++;return 22;
}
static int check(int value,int line) {
    if(!value)fprintf(stderr,"check failed at %d\n",line);
    return value;
}
#define CHECK(v) do { if(!check((v),__LINE__))return 1; } while(0)

int main(void) {
    FxPtpIdentity identity={0};
    CHECK(fx_zf_ptp_init(&owner,&identity)==FX_OK);
    CHECK(owner.identity==&identity && identity.firmware==301u);
    CHECK(fx_zf_ptp_init(&owner,0)==FX_ARGUMENT);
    uint8_t out[32]={0},ops[32]={0};
    CHECK((uintptr_t)fx_native_ptp_profile.session==UINT64_C(0xa0498bf1c));
    CHECK((uintptr_t)fx_native_ptp_profile.operations==UINT64_C(0xa0498c7c8));
    CHECK(fx_native_ptp_profile.operations_capacity==0x200u);
    CHECK(!fx_native_ptp_profile.active() && !fx_native_ptp_profile.state());
    enabled=1;policy=0;
    CHECK(!fx_native_ptp_profile.active() && !fx_native_ptp_profile.state());
    atomic_init(&owner.enabled,1u);
    policy=1;
#if FX_ZF_C301_PTP_EXPERIMENT
    authorization_available=0;
    CHECK(!fx_native_ptp_profile.active() && !fx_native_ptp_profile.state());
    authorization_available=1;
    CHECK(fx_native_ptp_profile.active() && fx_native_ptp_profile.state()==&owner);
    const FxNativePtpDescriptor *d=fx_zf_ptp_lookup(0x95e0);
    const FxNativePtpDescriptor *submit=fx_zf_ptp_lookup(0x95e1);
    CHECK(d && d->opcode==0x95e0 && submit && submit->buffer);
    CHECK(d->preflight(d,out)==1 && new_lookup==0);
    uint8_t *buffer=0;uint32_t bytes=0;
    CHECK(submit->buffer(submit,out,&buffer,&bytes)==1 && buffer && bytes==12 && buffer_calls==1);
    CHECK(submit->execute(submit,out)==1 && execute_calls==1);
    CHECK(!atomic_load(&authorization.request) && atomic_load(&authorization.generation)==UINT32_MAX);
    CHECK(fx_zf_ptp_lookup(0x95e2)->preflight(fx_zf_ptp_lookup(0x95e2),out)==1);
    CHECK(fx_zf_ptp_lookup(0x95e2)->execute(fx_zf_ptp_lookup(0x95e2),out)==1 && execute_calls==2);
    CHECK(submit->buffer(submit,out,&buffer,&bytes)==1 && buffer_calls==2);
    policy=0;
    CHECK(submit->execute(submit,0)==0 && execute_calls==2);
    CHECK(submit->buffer(submit,out,&buffer,&bytes)==0 && !buffer && !bytes && buffer_calls==2);
    policy=1;
    CHECK(submit->execute(submit,0)==0 && execute_calls==2);
    CHECK(submit->preflight(submit,out)==1);
    policy=0;
    CHECK(submit->execute(submit,0)==0 && execute_calls==2);
    policy=1;
    CHECK(submit->execute(submit,0)==0 && execute_calls==2);
    CHECK(fx_zf_ptp_operations((uint8_t *)(uintptr_t)0xa0498c7c8u,0x200u,
        (const uint8_t *)(uintptr_t)0xa0498c7c8u)==0);
    CHECK(fx_zf_ptp_operations(out,sizeof(out),ops)==22);
    CHECK(new_lookup==4 && new_session==0 && new_ops==1);
    CHECK(old_lookup==0 && old_session==0 && old_ops==0);
    policy=0;
    CHECK(!fx_native_ptp_profile.active() && !fx_native_ptp_profile.state());
    CHECK(fx_zf_ptp_lookup(0x95e0)==d);
    buffer=(uint8_t *)&owner;bytes=12;
    CHECK(submit->buffer(submit,out,&buffer,&bytes)==0 && !buffer && !bytes && buffer_calls==2);
    CHECK(d->preflight(d,out)==0 && execute_calls==2);
    policy=1;atomic_store(&owner.enabled,0u);
    CHECK(!fx_native_ptp_profile.active() && !fx_native_ptp_profile.state());
    /* A disconnect between buffer entry and return cannot mint a new token. */
    atomic_store(&owner.enabled,1u);policy=1;change_generation_in_buffer=1;
    buffer=(uint8_t *)&owner;bytes=12u;
    CHECK(submit->buffer(submit,out,&buffer,&bytes)==0 && !buffer && !bytes);
    CHECK(!atomic_load(&authorization.request));
    CHECK(!callback_locked);
#else
    CHECK(!fx_native_ptp_profile.active() && !fx_native_ptp_profile.state());
    CHECK(fx_zf_ptp_lookup(0x95e0)==&stock);
    fx_zf_ptp_session(7);
    CHECK(fx_zf_ptp_operations(out,sizeof(out),ops)==11);
    CHECK(old_lookup==1 && old_session==1 && old_ops==1);
    CHECK(new_lookup==0 && new_session==0 && new_ops==0);
#endif
    puts("PASS zf_ptp_adapter");
    return 0;
}
