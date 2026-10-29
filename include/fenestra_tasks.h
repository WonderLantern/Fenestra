#ifndef FENESTRA_TASKS_H
#define FENESTRA_TASKS_H
#include "fenestra_platform.h"
/* Named core.tasks/v1 copied service. Native arguments reference retained
 * original/native objects, never exchange or module stack storage. */
#define FX_TASK_CONTEXT_BYTES 32u
#define FX_TASK_BUDGET (4u * 1024u * 1024u)
enum {FX_TASK_QUERY=1u,FX_TASK_POST,FX_TASK_STATUS,FX_TASK_FETCH,FX_TASK_CANCEL,FX_TASK_CAPACITY};
typedef struct {uint32_t abi,bytes,budget,used,peak,reserved_bytes,owner_groups,sessions,active_sessions,
    queued,queued_peak,running,completed,session_bytes,domain_bytes,reserved;} FxTaskCapacity;
_Static_assert(sizeof(FxTaskCapacity)==64u,"task capacity ABI");
enum {FX_TASK_IDLE,FX_TASK_QUEUED,FX_TASK_RUNNING,FX_TASK_DONE,FX_TASK_CLOSED};
typedef struct {
    char context[FX_TASK_CONTEXT_BYTES];
    uint64_t binding;
    uint32_t call_bytes,reserved;
    _Alignas(16) uint8_t call[sizeof(FxNativeCallFrame)];
} FxTaskPost;
typedef struct {
    uint32_t abi,bytes,operations,capacity;
    uint64_t task;
    char context[FX_TASK_CONTEXT_BYTES];
} FxTaskInfo;
typedef struct {
    uint32_t abi,bytes;
    uint64_t ticket;
    uint32_t state,result;
    uint64_t task,value[2];
} FxTaskStatus;
/* Core-only domain; profiles select a real original task through capture. */
typedef struct {
    _Atomic uintptr_t task,head;
    uintptr_t tail; /* Queue links and counters below are domain-lock serialized. */
    _Atomic uint32_t running,lock,requested_bytes,peak_bytes,completed;
    uint32_t owner_groups,sessions,active_sessions,queued,queued_peak,retained_bytes;
    FxAllocationObserver (*owner_account)(FxPlatformOwner *);
} FxTaskDomain;
typedef struct FxTaskSession FxTaskSession;
FxResult fx_tasks_capture(FxTaskDomain *,uintptr_t task);
int fx_tasks_pending(const FxTaskDomain *);
FxResult fx_tasks_poll(FxTaskDomain *,uintptr_t task);
FxResult fx_tasks_attach(FxTaskDomain *,FxPlatformOwner *,uint64_t token,
                         void *(*allocate)(void *,uint32_t),void (*release)(void *,void *),void *,FxTaskSession **);
FxResult fx_tasks_resource(FxTaskSession *,uint32_t operation,FxPlatformOwner *,uint64_t token,
                           const FxServiceRequest *,FxServiceReply *);
/* Native adapter provided only by profiles that actually bind a UI task. */
FxResult fx_native_tasks_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,
                                  uint64_t *,const FxServiceRequest *,FxServiceReply *);
#endif
