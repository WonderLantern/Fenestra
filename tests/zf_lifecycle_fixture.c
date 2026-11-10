#include <assert.h>
#include <pthread.h>
#include "lifecycle.h"

static FxZfLifecycle *published;
FxZfLifecycle *fx_zf_runtime_lifecycle(void){return published;}

static void permutations(uint32_t *events,unsigned first) {
    if(first==4) {
        FxZfLifecycle life={0};
        for(unsigned i=0;i<4;i++) {
            fx_zf_lifecycle_record(&life,events[i]);
            if(events[i]==8u)assert(!fx_zf_lifecycle_accepts_work(&life));
        }
        uint32_t final=fx_zf_lifecycle_take(&life);
        assert((final&FX_ZF_LIFE_STOP) && !(final&FX_ZF_LIFE_CARD));
        fx_zf_lifecycle_record(&life,20);fx_zf_lifecycle_record(&life,25);
        assert(!fx_zf_lifecycle_accepts_work(&life));
        assert(fx_zf_lifecycle_take(&life)==(final&~(uint32_t)FX_ZF_LIFE_PENDING));
        return;
    }
    for(unsigned i=first;i<4;i++) {
        uint32_t t=events[first];events[first]=events[i];events[i]=t;
        permutations(events,first+1);
        t=events[first];events[first]=events[i];events[i]=t;
    }
}
typedef struct {FxZfLifecycle *life;uint32_t event;} Work;
static void *producer(void *argument) {
    Work *work=argument;
    for(unsigned i=0;i<20000;i++)fx_zf_lifecycle_record(work->life,work->event);
    return 0;
}
static void *consumer(void *argument) {
    FxZfLifecycle *life=argument;
    for(unsigned i=0;i<20000;i++) {
        uint32_t word=fx_zf_lifecycle_take(life);
        if(word&FX_ZF_LIFE_STOP)assert(!(word&FX_ZF_LIFE_CARD));
    }
    return 0;
}
int main(void) {
    fx_zf_after_startup_complete(20,0); /* No published owner: no dereference. */
    fx_zf_before_bs_shutdown_event(8,1);
    assert(!fx_zf_lifecycle_accepts_work(0) && !fx_zf_lifecycle_take(0));
    FxZfLifecycle life={0};published=&life;
    fx_zf_after_card_mount(25,0);assert(!fx_zf_lifecycle_accepts_work(&life));
    fx_zf_after_startup_complete(20,0);assert(fx_zf_lifecycle_accepts_work(&life));
    uint32_t v=fx_zf_lifecycle_take(&life);
    assert((v&FX_ZF_LIFE_PENDING)==(FX_ZF_LIFE_START_PENDING|FX_ZF_LIFE_CARD_PENDING));
    assert(fx_zf_lifecycle_take(&life)==(FX_ZF_LIFE_STARTED|FX_ZF_LIFE_CARD));
    fx_zf_before_card_remove(24,0);assert(!fx_zf_lifecycle_accepts_work(&life));
    assert(fx_zf_lifecycle_take(&life)==(FX_ZF_LIFE_STARTED|FX_ZF_LIFE_CARD_PENDING));
    fx_zf_after_card_mount(25,1);assert(fx_zf_lifecycle_accepts_work(&life));
    fx_zf_before_bs_shutdown_event(8,0);assert(!fx_zf_lifecycle_accepts_work(&life));
    fx_zf_lifecycle_take(&life);
    fx_zf_after_startup_complete(20,0);fx_zf_after_card_mount(25,0);
    assert(fx_zf_lifecycle_take(&life)==(FX_ZF_LIFE_STARTED|FX_ZF_LIFE_STOP));
    uint32_t events[]={8,20,24,25};permutations(events,0);
    FxZfLifecycle concurrent={0};Work works[]={{&concurrent,20},{&concurrent,25},{&concurrent,24},{&concurrent,8}};
    pthread_t threads[5];
    for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],0,producer,&works[i]));
    assert(!pthread_create(&threads[4],0,consumer,&concurrent));
    for(unsigned i=0;i<5;i++)assert(!pthread_join(threads[i],0));
    assert(fx_zf_lifecycle_take(&concurrent)&FX_ZF_LIFE_STOP);
    assert(!fx_zf_lifecycle_accepts_work(&concurrent));
    return 0;
}
