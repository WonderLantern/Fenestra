#if !defined(FX_ZF_EXPERIMENTAL_OWNER_FAULT) || FX_ZF_EXPERIMENTAL_OWNER_FAULT != 1
#error "Zf owner fault handling is experimental, not camera recovery"
#endif
#include "owner_storage.h"
_Static_assert(ATOMIC_LLONG_LOCK_FREE==2,"Zf first-fault pair requires lock-free 64-bit atomic");
void fx_zf_owner_fault(FxZfOwnerStorage *state,const FxPlatformOwner *module,FxResult error) {
    if(!state)return;
    if(error==FX_OK)error=FX_INTEGRITY;
    uint64_t zero=0u,record=((uint64_t)(module?module->owner:0u)<<32)|(uint32_t)error;
    (void)atomic_compare_exchange_strong_explicit(&state->first_fault,&zero,record,
            memory_order_acq_rel,memory_order_acquire);
    fx_zf_lifecycle_record(&state->lifecycle,8u);
}
