#if !defined(FX_ZF_EXPERIMENTAL_LIFECYCLE) || FX_ZF_EXPERIMENTAL_LIFECYCLE != 1
#error "Zf lifecycle inbox is experimental; no target owner has been installed"
#endif
#include "lifecycle.h"

_Static_assert(ATOMIC_INT_LOCK_FREE==2 && sizeof(uint32_t)==sizeof(unsigned int),
               "SYSCTL inbox requires lock-free native 32-bit atomics");

void fx_zf_lifecycle_record(FxZfLifecycle *s,uint32_t event) {
    if(!s || (event!=8u && event!=20u && event!=24u && event!=25u))return;
    uint32_t old=atomic_load_explicit(&s->word,memory_order_acquire);
    for(;;) {
        if(old&FX_ZF_LIFE_STOP)return;
        uint32_t next=old;
        if(event==8u)next=(old&~(uint32_t)FX_ZF_LIFE_CARD)|FX_ZF_LIFE_STOP|FX_ZF_LIFE_STOP_PENDING;
        else if(event==20u)next=old|FX_ZF_LIFE_STARTED|FX_ZF_LIFE_START_PENDING;
        else if(event==25u)next=old|FX_ZF_LIFE_CARD|FX_ZF_LIFE_CARD_PENDING;
        else next=(old&~(uint32_t)FX_ZF_LIFE_CARD)|FX_ZF_LIFE_CARD_PENDING;
        if(atomic_compare_exchange_weak_explicit(&s->word,&old,next,memory_order_acq_rel,memory_order_acquire))return;
    }
}
uint32_t fx_zf_lifecycle_take(FxZfLifecycle *s) {
    return s?atomic_fetch_and_explicit(&s->word,~(uint32_t)FX_ZF_LIFE_PENDING,memory_order_acq_rel):0u;
}
int fx_zf_lifecycle_accepts_work(const FxZfLifecycle *s) {
    if(!s)return 0;
    uint32_t state=atomic_load_explicit(&s->word,memory_order_acquire);
    return (state&(FX_ZF_LIFE_STARTED|FX_ZF_LIFE_CARD|FX_ZF_LIFE_STOP))==
        (FX_ZF_LIFE_STARTED|FX_ZF_LIFE_CARD);
}
void fx_zf_before_bs_shutdown_event(uint32_t event,uint32_t sync) {
    (void)sync;if(event==8u)fx_zf_lifecycle_record(fx_zf_runtime_lifecycle(),event);
}
void fx_zf_after_startup_complete(uint32_t event,uint32_t sync) {
    (void)sync;if(event==20u)fx_zf_lifecycle_record(fx_zf_runtime_lifecycle(),event);
}
void fx_zf_after_card_mount(uint32_t event,uint32_t sync) {
    (void)sync;if(event==25u)fx_zf_lifecycle_record(fx_zf_runtime_lifecycle(),event);
}
void fx_zf_before_card_remove(uint32_t event,uint32_t sync) {
    (void)sync;if(event==24u)fx_zf_lifecycle_record(fx_zf_runtime_lifecycle(),event);
}
