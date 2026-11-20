#include "lifecycle.h"
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"SYSCTL inbox needs lock-free atomics");
void fx_z63_lifecycle_record(FxZ63Lifecycle *s,uint32_t event) {
    if(!s || (event!=8u && event!=20u && event!=24u && event!=25u))return;
    uint32_t old=atomic_load_explicit(&s->word,memory_order_acquire);
    for(;;) {
        if(old&FX_Z63_STOP)return;
        uint32_t next=old;
        if(event==8u)next=(old&~(uint32_t)FX_Z63_CARD)|FX_Z63_STOP|FX_Z63_STOP_PENDING;
        else if(event==20u)next=old|FX_Z63_STARTED|FX_Z63_START_PENDING;
        else if(event==25u)next=old|FX_Z63_CARD|FX_Z63_CARD_PENDING;
        else next=(old&~(uint32_t)FX_Z63_CARD)|FX_Z63_CARD_PENDING;
        if(atomic_compare_exchange_weak_explicit(&s->word,&old,next,memory_order_acq_rel,memory_order_acquire))return;
    }
}
uint32_t fx_z63_lifecycle_take(FxZ63Lifecycle *s) {
    return s?atomic_fetch_and_explicit(&s->word,~(uint32_t)FX_Z63_PENDING,memory_order_acq_rel):0u;
}
int fx_z63_lifecycle_accepts_work(const FxZ63Lifecycle *s) {
    if(!s)return 0;
    uint32_t state=atomic_load_explicit(&s->word,memory_order_acquire);
    return (state&(FX_Z63_STARTED|FX_Z63_CARD|FX_Z63_STOP))==(FX_Z63_STARTED|FX_Z63_CARD);
}
