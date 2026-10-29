#ifndef FENESTRA_EVENTS_H
#define FENESTRA_EVENTS_H
#include "fenestra_services.h"
/* rtos.events/v1. All requests are copied and nonblocking. Native event flags
 * coalesce bits; they are not a message queue or a count of notifications. */
enum {FX_EVENT_QUERY=1u,FX_EVENT_CREATE,FX_EVENT_SET,FX_EVENT_CLEAR,
      FX_EVENT_POLL,FX_EVENT_EXPORT,FX_EVENT_RELEASE,FX_EVENT_RECLAIM};
enum {FX_EVENT_ANY=1u,FX_EVENT_ALL=2u,
      FX_EVENT_CLEAR_ALL=0x10u,FX_EVENT_CLEAR_MATCH=0x20u};
typedef struct {uint64_t handle;uint32_t bits,mode;} FxEventRequest;
typedef struct {uint32_t abi,bytes,operations,capacity,active,retained,modes,reserved;} FxEventInfo;
/* CREATE: bits=initial state, mode=0. Returns owner-local handle in value[0].
 * SET/CLEAR: bits to set/clear. POLL: ANY/ALL and optional CLEAR_ALL/MATCH;
 * value[0] is the complete state BEFORE consumption, EMPTY means no match.
 * value[3] always preserves a native error as a sign-extended int32_t.
 * EXPORT: value[1]=retained signal cookie; value[2]=native event ID.
 * rtos.event.signal(cookie,bits) is callable from a native task/IRQ hook on
 * original mappings. It selects the original task/IRQ setter and returns
 * its int32_t error. Native IRQ-masked restrictions still apply.
 * RELEASE closes copied access. Exported IDs/cookies remain valid. RECLAIM
 * asserts ALL native users/waiters have quiesced, deletes and permits reuse;
 * it also works after RELEASE. Last binding close releases unexported flags
 * and retains exported ones. Panic retains all flags until reboot.
 * No module/exchange pointer is retained and no Flash is accessed. */
int64_t fx_native_event_signal(uintptr_t cookie,uint32_t bits);
#endif
