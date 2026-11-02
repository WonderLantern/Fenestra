#include <assert.h>
#include <pthread.h>
#include "../src/platform/z63_c201/lifecycle.h"
static FxZ63Lifecycle state;
static void *events(void *unused) {
    (void)unused;
    for(unsigned i=0;i<10000u;++i) {
        fx_z63_lifecycle_record(&state,20u);
        fx_z63_lifecycle_record(&state,25u);
    }
    return 0;
}
int main(void) {
    assert(!fx_z63_lifecycle_accepts_work(&state));
    fx_z63_lifecycle_record(&state,19u);
    assert(!atomic_load(&state.word));
    fx_z63_lifecycle_record(&state,25u);
    assert(!fx_z63_lifecycle_accepts_work(&state));
    fx_z63_lifecycle_record(&state,20u);
    assert(fx_z63_lifecycle_accepts_work(&state));
    uint32_t value=fx_z63_lifecycle_take(&state);
    assert((value&FX_Z63_PENDING)==(FX_Z63_START_PENDING|FX_Z63_CARD_PENDING));
    assert(!(fx_z63_lifecycle_take(&state)&FX_Z63_PENDING));
    fx_z63_lifecycle_record(&state,24u);
    assert(!fx_z63_lifecycle_accepts_work(&state));
    pthread_t worker;assert(!pthread_create(&worker,0,events,0));
    fx_z63_lifecycle_record(&state,8u);
    assert(!pthread_join(worker,0));
    assert(atomic_load(&state.word)&FX_Z63_STOP);
    assert(!(atomic_load(&state.word)&FX_Z63_CARD));
    assert(!fx_z63_lifecycle_accepts_work(&state));
    return 0;
}
