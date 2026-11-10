/* Zf C3.01 PTP adapter. Native lookup/session/operations are attached when
 * FX_ZF_C301_PTP_EXPERIMENT is selected. The owner and user-enable gates
 * admit Fenestra operations; other operations retain the stock endpoint. */
#include "ptp.h"
#include "ptp_admission.h"
#include "fenestra_platform_native.h"
#include "fenestra_native_linux.h"
#include <stdint.h>

#define FN(a,t) ((t)(uintptr_t)(a))
#ifndef FX_ZF_C301_PTP_EXPERIMENT
#define FX_ZF_C301_PTP_EXPERIMENT 0
#endif

FxResult fx_zf_ptp_init(FxPtp *p,FxPtpIdentity *identity) {
    const FxPlatform *target=fx_platform_target();
    if(!p || !identity || !target)return FX_ARGUMENT;
    fx_ptp_init(p);
    *identity=(FxPtpIdentity){301u,FX_ZF_PTP_CAPS,FX_ZF_PTP_FLAGS,
        "Nikon Zf","C 3.01",target->identity,target->module_identity,target->identity};
    p->identity=identity;
    return FX_OK;
}
FxResult fx_zf_ptp_bind_owner(void) {
    FxPtp *p=fx_zf_ptp_owner();
    return p && p->identity ? fx_zf_ptp_bind(p->identity) : FX_STATE;
}
#if FX_ZF_C301_PTP_EXPERIMENT != 0 && FX_ZF_C301_PTP_EXPERIMENT != 1
#error "FX_ZF_C301_PTP_EXPERIMENT must be 0 or 1"
#endif
_Static_assert(FX_ZF_C301_PTP_EXPERIMENT == 0 || FX_ZF_C301_PTP_EXPERIMENT == 1,
               "FX_ZF_C301_PTP_EXPERIMENT must be 0 or 1");

#if FX_ZF_C301_PTP_EXPERIMENT
static void clear_authorization(void);

static int allowed(void) {
    return fx_zf_ptp_user_enabled() && fx_zf_ptp_policy_permits();
}
static FxPtp *ptp_state(void) {
    if (!allowed()) {
        clear_authorization();
        return 0;
    }
    FxPtp *owner=fx_zf_ptp_owner();
    if (!owner || !fx_zf_ptp_authorization() || !atomic_load_explicit(&owner->enabled,memory_order_acquire)) {
        clear_authorization();
        return 0;
    }
    return owner;
}
static int ptp_active(void) {
    return ptp_state() != 0;
}
#else
static FxPtp *ptp_state(void) { return 0; }
static int ptp_active(void) { return 0; }
#endif

#if FX_ZF_C301_PTP_EXPERIMENT
static void clear_authorization(void)
{
    FxZfPtpAuthorization *a=fx_zf_ptp_authorization();if(!a)return;
    atomic_store_explicit(&a->request, 0, memory_order_release);
    atomic_store_explicit(&a->generation, UINT32_MAX, memory_order_release);
}

static int range_overlaps(uintptr_t a, uint32_t bytes, uintptr_t b, uint32_t extent)
{
    if (!bytes || !extent || a > UINTPTR_MAX - bytes || b > UINTPTR_MAX - extent) return 0;
    return a < b + extent && b < a + bytes;
}

/* Stock Zf performs descriptor preflight before its global 11-slot filter,
 * then looks up the descriptor a second time for execute. Keep descriptors
 * stable even when policy changes; the local preflight only records a
 * session/generation token, while the shared descriptor keeps the payload
 * validation and execution policy. */
static uint32_t ptp_buffer(const FxNativePtpDescriptor *d,const uint8_t *q,uint8_t **out,uint32_t *bytes) {
    if (!out || !bytes) return 0;
    *out=0;*bytes=0;
    if(!d || !q || !fx_zf_ptp_enter())return 0;
    FxPtp *owner = ptp_state();
    FxZfPtpAuthorization *a=fx_zf_ptp_authorization();
    if (!owner || !a){fx_zf_ptp_leave();return 0;}
    uint32_t generation=atomic_load_explicit(&owner->generation,memory_order_acquire);
    clear_authorization();
    const FxNativePtpDescriptor *shared=fx_native_ptp_lookup(d->opcode);
    if (!shared || !shared->buffer || !shared->buffer(shared,q,out,bytes) ||
        generation!=atomic_load_explicit(&owner->generation,memory_order_acquire)) {
        *out=0;*bytes=0;fx_zf_ptp_leave();return 0;
    }
    atomic_store_explicit(&a->request,(uintptr_t)q,memory_order_release);
    atomic_store_explicit(&a->generation,generation,memory_order_release);
    fx_zf_ptp_leave();
    return 1;
}
static uint32_t ptp_preflight(const FxNativePtpDescriptor *d,const uint8_t *q) {
    if(!d || !q || !fx_zf_ptp_enter())return 0;
    FxPtp *owner = ptp_state();
    FxZfPtpAuthorization *a=fx_zf_ptp_authorization();
    if (!owner || !a){fx_zf_ptp_leave();return 0;}
    uint32_t generation=atomic_load_explicit(&owner->generation,memory_order_acquire);
    atomic_store_explicit(&a->request,(uintptr_t)q,memory_order_release);
    atomic_store_explicit(&a->generation,generation,memory_order_release);
    fx_zf_ptp_leave();
    return 1u;
}
static uint32_t ptp_execute(const FxNativePtpDescriptor *d,const uint8_t *q) {
    int entered=fx_zf_ptp_enter();
    FxPtp *owner = ptp_state();
    FxZfPtpAuthorization *a=fx_zf_ptp_authorization();
    if (entered && owner && a && d && q &&
        atomic_load_explicit(&a->request,memory_order_acquire) == (uintptr_t)q &&
        atomic_load_explicit(&a->generation,memory_order_acquire) ==
            atomic_load_explicit(&owner->generation,memory_order_acquire)) {
        clear_authorization();
        const FxNativePtpDescriptor *shared=fx_native_ptp_lookup(d->opcode);
        if (shared && shared->execute) {
            uint32_t result=shared->execute(shared,q);
            fx_zf_ptp_leave();return result;
        }
    }
    if(entered){clear_authorization();fx_zf_ptp_leave();}
    if (q) fx_native_ptp_profile.response(q,0x2005u,0u,0,d && d->direction==1u?2u:0u);
    return 0;
}
static const FxNativePtpDescriptor ptp_rows[]={
    {FX_PTP_HELLO,1u,0,0,0,0,ptp_preflight,ptp_execute,0},
    {FX_PTP_SUBMIT,2u,0,0,0,ptp_buffer,ptp_preflight,ptp_execute,0},
    {FX_PTP_FETCH,1u,0,0,0,0,ptp_preflight,ptp_execute,0},
    {FX_PTP_CONTROL,1u,0,0,0,0,ptp_preflight,ptp_execute,0}
};
#endif

/* Address values and the three original entry preimages are specific to the
 * hash-pinned C3.01 application. See the offline PTP research receipt. */
const FxNativePtpProfile fx_native_ptp_profile={
    ptp_state,ptp_active,
    FN(UINT64_C(0x407f55dc),uint8_t *(*)(void)),
    FN(UINT64_C(0x407f5618),uint32_t (*)(void)),
    FN(UINT64_C(0x407f56c0),const uint8_t *(*)(void)),
    FN(UINT64_C(0x407f56cc),void (*)(const uint8_t *,uint32_t,uint32_t,const void *,uint32_t)),
    FN(UINT64_C(0x407f59bc),uint32_t (*)(const uint8_t *,const uint8_t *,uint32_t,uint32_t)),
    fx_zf_ptp_original_lookup,fx_zf_ptp_original_session,fx_zf_ptp_original_operations,fx_linux_revoke,
    (const volatile uint32_t *)(uintptr_t)UINT64_C(0xa0498bf1c),
    (const uint8_t *)(uintptr_t)UINT64_C(0xa0498c7c8),0x200u
};

const FxNativePtpDescriptor *fx_zf_ptp_lookup(uint32_t opcode) {
#if FX_ZF_C301_PTP_EXPERIMENT
    if (opcode>=FX_PTP_HELLO && opcode<=FX_PTP_CONTROL)
        return &ptp_rows[opcode-FX_PTP_HELLO];
    return fx_zf_ptp_original_lookup(opcode);
#else
    return fx_zf_ptp_original_lookup(opcode);
#endif
}
void fx_zf_ptp_session(uint32_t session) {
#if FX_ZF_C301_PTP_EXPERIMENT
    /* The policy gate can hide state() while an older owner still exists.
     * Revoke that owner's generation on a session change regardless of the
     * current policy, then forward the stock notification exactly once. */
    if (*fx_native_ptp_profile.session != session) {
        clear_authorization();
        fx_ptp_disconnect(fx_zf_ptp_owner());
        fx_linux_revoke();
    }
    fx_zf_ptp_original_session(session);
#else
    fx_zf_ptp_original_session(session);
#endif
}
uint32_t fx_zf_ptp_operations(uint8_t *out,uint32_t capacity,const uint8_t *operations) {
#if FX_ZF_C301_PTP_EXPERIMENT
    if (operations == fx_native_ptp_profile.operations && out && range_overlaps(
        (uintptr_t)out,capacity,(uintptr_t)fx_native_ptp_profile.operations,
        fx_native_ptp_profile.operations_capacity))
        return 0;
    return fx_native_ptp_operations(out,capacity,operations);
#else
    return fx_zf_ptp_original_operations(out,capacity,operations);
#endif
}
