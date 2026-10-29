#ifndef FENESTRA_SCHEDULE_H
#define FENESTRA_SCHEDULE_H
#include "fenestra_services.h"
/* core.schedule/v1. A one-shot continuation wait for this module's worker.
 * Capture a ticket BEFORE draining queues, then WAIT with that ticket. A
 * notification after capture prevents sleeping, even if it preceded WAIT.
 * Native timeout units are original RTOS ticks, not the API monotonic ms. */
enum {FX_SCHEDULE_QUERY=1u,FX_SCHEDULE_WAIT};
typedef struct {uint64_t observed;uint32_t timeout_ticks,reserved;} FxScheduleWait;
typedef struct {uint32_t abi,bytes,operations,features,max_timeout_ticks,period_ms,native_ready,reserved;} FxScheduleInfo;
/* timeout_ticks=UINT32_MAX is unbounded, 0 polls. Other values <=INT32_MAX.
 * observed=0 obtains an initial ticket without sleeping. Reply.value[0] is
 * the new ticket; value[1] reports a changed ticket, value[2] a timeout.
 * Drain relevant queues, then wait again using the returned ticket. Native
 * housekeeping can still run without executing the module. The finite
 * timeout is a native waiting budget, excluding that housekeeping's CPU.
 * Stop/Panic wake the worker but never resume a stopped module. */
_Static_assert(sizeof(FxScheduleWait)==16u,"schedule copied request ABI");
#endif
