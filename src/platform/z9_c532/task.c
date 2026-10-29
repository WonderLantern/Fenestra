#include "task.h"
#include "fenestra_module.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#define MAGIC UINT32_C(0x465a3954)
typedef struct {
    uint32_t attributes,reserved;uintptr_t context,entry;
    uint32_t priority,reserved2;uint64_t stack_bytes;uintptr_t stack;
    uint32_t processor,affinity;
} Task;
_Static_assert(sizeof(Task)==56,"C532 original task creation record");
_Static_assert(offsetof(Task,entry)==16 && offsetof(Task,stack_bytes)==32 && offsetof(Task,affinity)==52,"C532 argument offsets");
FxResult fx_z9_task_start(FxZ9TaskScope *s,void (*entry)(void *),void *context,void *stack,uint32_t bytes) {
    if(!s || !entry || ((uintptr_t)entry&3u) || !stack || ((uintptr_t)stack&15u) ||
       bytes<4096u || bytes>FX_MODULE_STACK_MAX || (bytes&15u) || (uintptr_t)stack>UINTPTR_MAX-bytes)return FX_ARGUMENT;
    if(s->magic || s->phase || s->id || s->stack || s->stack_bytes)return FX_BUSY;
    *s=(FxZ9TaskScope){MAGIC,FX_Z9_TASK_EMPTY,0u,0u,-1,0u,(uintptr_t)stack,bytes};
    Task task={0u,0u,(uintptr_t)context,(uintptr_t)entry,28u,0u,bytes,(uintptr_t)stack,1u,UINT32_MAX};
    int32_t id=FN(UINT64_C(0x40185064),int32_t (*)(const Task *))(&task);
    if(id<1){s->native_error=(uint32_t)id;s->phase=FX_Z9_TASK_FAILED;return FX_IO;}
    s->id=id;s->phase=FX_Z9_TASK_CREATED;
    int32_t result=FN(UINT64_C(0x401855e0),int32_t (*)(int32_t))(id);
    if(result){s->native_error=(uint32_t)result;s->phase=FX_Z9_TASK_FAILED;return FX_IO;}
    s->phase=FX_Z9_TASK_RUNNING;return FX_OK;
}
FxResult fx_z9_task_delay_ticks(uint32_t ticks) {
    if(!ticks || ticks>1000u)return FX_ARGUMENT;
    return FN(UINT64_C(0x40187b00),int32_t (*)(uint32_t))(ticks)?FX_IO:FX_OK;
}
