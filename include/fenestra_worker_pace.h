#ifndef FENESTRA_WORKER_PACE_H
#define FENESTRA_WORKER_PACE_H
#include "fenestra_services.h"
/* A reply is work, not a periodic timer tick. Continue fresh successful
 * requests, but yield to the original scheduler after a bounded burst.
 * EMPTY/BUSY/errors and replays keep the existing idle polling interval.
 * Explicit WAIT, interrupted EL0, stop and fault remain worker decisions. */
enum {FX_WORKER_POLL,FX_WORKER_CONTINUE,FX_WORKER_YIELD};
#define FX_WORKER_BURST 8u
typedef struct {uint32_t completed;} FxWorkerPace;
static inline uint32_t fx_worker_pace_reply(FxWorkerPace *pace,uint64_t previous,
    uint64_t sequence,const FxServiceReply *reply) {
    if(previous==UINT64_MAX || !sequence || sequence!=previous+1u ||
       reply->sequence!=sequence || reply->result!=FX_OK) {
        pace->completed=0u;return FX_WORKER_POLL;
    }
    if(++pace->completed>=FX_WORKER_BURST) {
        pace->completed=0u;return FX_WORKER_YIELD;
    }
    return FX_WORKER_CONTINUE;
}
#endif
