#ifndef FENESTRA_WORKER_WAIT_H
#define FENESTRA_WORKER_WAIT_H
#include "fenestra_platform.h"
#include "fenestra_schedule.h"
#include "fenestra_capacity.h"
/* Core-only native wait port. Owned storage and its signal callback survive
 * until reboot. Initialize/bind before publishing the owner. Only its worker
 * creates/clears/waits; task/IRQ producers only advance ticket and SET. */
typedef struct {
    uint32_t initialized,period_ms;
    uintptr_t create,set,set_irq,clear,wait,is_irq;
    _Atomic int32_t event;
    _Atomic uint64_t epoch;
    _Atomic uint32_t error;
    _Atomic int32_t native_error;
    uint64_t observed;
    uint32_t remaining,armed,timed_out,resumed;
    _Atomic uint64_t sleeps,signals,timeouts;
    FxCapacityDomain *event_capacity;FxCapacityClaim event_claim;
} FxWorkerWait;
FxResult fx_worker_wait_init(FxWorkerWait *,const FxPlatform *,uint32_t period_ms);
/* Complete the original function profile before the worker creates its event.
 * A prepared port can accept early lifecycle signals without losing tickets. */
FxResult fx_worker_wait_profile(FxWorkerWait *,const FxPlatform *);
FxResult fx_worker_wait_bind(FxPlatformOwner *,FxWorkerWait *);
void fx_worker_wait_signal(void *);
uint64_t fx_worker_wait_ticket(const FxWorkerWait *);
/* Core worker, original tables and no owner/native writer lock held.
 * quantum limits one native wait for resource/tick housekeeping. */
FxResult fx_worker_wait_step(FxWorkerWait *,uint64_t observed,uint32_t ticks,uint32_t *expired);
/* One armed WAIT: returns EMPTY while housekeeping should continue, OK when
 * its continuation can run; updates the copied reply only on completion. */
FxResult fx_worker_wait_continue(FxWorkerWait *,uint32_t quantum,FxServiceReply *,FxServiceOwner *);
FxResult fx_schedule_resource(uint32_t,FxPlatformOwner *,uint64_t *,const FxServiceRequest *,FxServiceReply *);
#endif
