#include "lifecycle.h"

_Static_assert(ATOMIC_INT_LOCK_FREE==2 && sizeof(uint32_t)==sizeof(unsigned int),
               "SYSCTL inbox requires lock-free native 32-bit atomics");

void fx_z8_lifecycle_record(FxZ8Lifecycle *s,uint32_t event) {
    if(!s || (event!=FX_Z8_LIFE_EVENT_BS_SHUTDOWN && event!=FX_Z8_LIFE_EVENT_STARTUP_COMPLETE &&
              event!=FX_Z8_LIFE_EVENT_CARD_REMOVE && event!=FX_Z8_LIFE_EVENT_MOUNT_COMPLETE))return;
    uint32_t old=atomic_load_explicit(&s->word,memory_order_acquire);
    for(;;) {
        if(old&FX_Z8_LIFE_STOP)return;
        uint32_t next=old;
        if(event==FX_Z8_LIFE_EVENT_BS_SHUTDOWN)
            next=(old&~(uint32_t)FX_Z8_LIFE_CARD)|FX_Z8_LIFE_STOP|FX_Z8_LIFE_STOP_PENDING;
        else if(event==FX_Z8_LIFE_EVENT_STARTUP_COMPLETE)
            next=old|FX_Z8_LIFE_STARTED|FX_Z8_LIFE_START_PENDING;
        else if(event==FX_Z8_LIFE_EVENT_MOUNT_COMPLETE)
            next=old|FX_Z8_LIFE_CARD|FX_Z8_LIFE_CARD_PENDING;
        else /* CARD_REMOVE */
            next=(old&~(uint32_t)FX_Z8_LIFE_CARD)|FX_Z8_LIFE_CARD_PENDING;
        if(atomic_compare_exchange_weak_explicit(&s->word,&old,next,memory_order_acq_rel,memory_order_acquire))return;
    }
}
uint32_t fx_z8_lifecycle_take(FxZ8Lifecycle *s) {
    return s?atomic_fetch_and_explicit(&s->word,~(uint32_t)FX_Z8_LIFE_PENDING,memory_order_acq_rel):0u;
}
int fx_z8_lifecycle_accepts_work(const FxZ8Lifecycle *s) {
    if(!s)return 0;
    uint32_t state=atomic_load_explicit(&s->word,memory_order_acquire);
    return (state&(FX_Z8_LIFE_STARTED|FX_Z8_LIFE_CARD|FX_Z8_LIFE_STOP))==
        (FX_Z8_LIFE_STARTED|FX_Z8_LIFE_CARD);
}
void fx_z8_before_bs_shutdown_event(uint32_t event,uint32_t sync) {
    (void)sync;if(event==FX_Z8_LIFE_EVENT_BS_SHUTDOWN)fx_z8_lifecycle_record(fx_z8_runtime_lifecycle(),event);
}
void fx_z8_after_startup_complete(uint32_t event,uint32_t sync) {
    (void)sync;if(event==FX_Z8_LIFE_EVENT_STARTUP_COMPLETE)fx_z8_lifecycle_record(fx_z8_runtime_lifecycle(),event);
}
void fx_z8_after_card_mount(uint32_t event,uint32_t sync) {
    (void)sync;if(event==FX_Z8_LIFE_EVENT_MOUNT_COMPLETE)fx_z8_lifecycle_record(fx_z8_runtime_lifecycle(),event);
}
void fx_z8_before_card_remove(uint32_t event,uint32_t sync) {
    (void)sync;if(event==FX_Z8_LIFE_EVENT_CARD_REMOVE)fx_z8_lifecycle_record(fx_z8_runtime_lifecycle(),event);
}
