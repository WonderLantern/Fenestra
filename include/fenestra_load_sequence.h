#ifndef FENESTRA_LOAD_SEQUENCE_H
#define FENESTRA_LOAD_SEQUENCE_H
#include "fenestra.h"
/* The controller waits only for retained image preparation, not module INIT
 * work or a user's APP lifetime. Yield uses the normal profile task delay.
 * No allocation, lock-held wait, new deadline or native task cancellation. */
typedef struct {
    void *context;
    FxResult (*poll)(void *);
    FxResult (*yield)(void *);
    int (*cancelled)(void *);
} FxLoadSequence;
static inline FxResult fx_load_sequence_wait(FxLoadSequence sequence) {
    if(!sequence.poll || !sequence.yield || !sequence.cancelled)return FX_ARGUMENT;
    for(;;) {
        if(sequence.cancelled(sequence.context))return FX_INTERRUPTED;
        FxResult result=sequence.poll(sequence.context);
        if(result!=FX_BUSY)return result;
        result=sequence.yield(sequence.context);
        if(result!=FX_OK)return result;
    }
}
#endif
