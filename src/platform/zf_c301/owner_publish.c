#if !defined(FX_ZF_EXPERIMENTAL_OWNER_PUBLISH) || FX_ZF_EXPERIMENTAL_OWNER_PUBLISH != 1
#error "Zf descriptor publication is experimental and not camera-qualified"
#endif
#include "owner_publish.h"
#include "fenestra_memory.h"
_Static_assert(ATOMIC_POINTER_LOCK_FREE==2 && sizeof(uintptr_t)==8,
               "Zf descriptor publication requires lock-free aligned 64-bit pointers");
FxResult fx_zf_owner_publish(FxNativePage *page,const FxNativeDescriptor *stock,const FxZfOwnerMemory *memory) {
    if(!page || !stock || !memory || !memory->allocate || !memory->release ||
       (uintptr_t)page%_Alignof(FxNativePage))return FX_ARGUMENT;
    _Atomic uintptr_t *slot=(_Atomic uintptr_t *)&page->descriptor;
    if(atomic_load_explicit(slot,memory_order_acquire)!=(uintptr_t)stock)return FX_STALE;
    FxZfOwnerStorage *candidate=memory->allocate(memory->context,(uint32_t)sizeof(*candidate));
    if(!candidate)return FX_BUDGET;
    if((uintptr_t)candidate%_Alignof(FxZfOwnerStorage)){
        memory->release(memory->context,candidate);return FX_INTEGRITY;
    }
    memset(candidate,0,sizeof(*candidate));
    FxResult result=fx_zf_owner_storage_prepare(candidate,page,stock);
    if(result==FX_OK) {
        /* Initialized before release publication, disabled until real target
         * user/policy gates and identity have been bound. */
        /* Bind identity once while the owner is unpublished. Consent and
         * activation remain disabled until an explicit local policy gate. */
        result=fx_zf_ptp_init(&candidate->ptp,&candidate->ptp_identity);
    }
    if(result==FX_OK) {
        fx_zf_lifecycle_record(&candidate->lifecycle,20u);
        uintptr_t expected=(uintptr_t)stock;
        if(atomic_compare_exchange_strong_explicit(slot,&expected,(uintptr_t)candidate,
                memory_order_acq_rel,memory_order_acquire)) {
            (void)fx_zf_ptp_bind_owner();
            return FX_OK;
        }
        result=FX_STALE;
    }
    memory->release(memory->context,candidate);
    return result;
}
