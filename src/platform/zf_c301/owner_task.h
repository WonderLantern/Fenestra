#ifndef FX_ZF_C301_OWNER_TASK_H
#define FX_ZF_C301_OWNER_TASK_H
#include "task.h"
#include "fenestra_module.h"
#include <stdatomic.h>
typedef struct {
    FxZfTaskScope scope;
    _Atomic uint32_t start_claimed,entered,steps,notices,last_word,finished,error;
    _Atomic uintptr_t task_pointer;
    int32_t (*delay)(uint32_t);
    uint32_t delay_ticks;
    uint8_t stack[FX_MODULE_STACK_DEFAULT] __attribute__((aligned(16)));
} FxZfOwnerTask;
struct FxZfOwnerStorage;
FxResult fx_zf_owner_task_start(struct FxZfOwnerStorage *,const FxZfNativeTaskOps *,
                              int32_t (*delay)(uint32_t),uint32_t ticks,uint32_t priority,uint32_t mode);
/* One owner-task iteration, not a module executor or GUI callback. */
int fx_zf_owner_task_step(struct FxZfOwnerStorage *);
#endif
