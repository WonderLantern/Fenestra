#ifndef FX_Z63_C201_TASK_H
#define FX_Z63_C201_TASK_H
#include "fenestra.h"
typedef struct {
    uint32_t magic,phase,native_error,reserved;
    int32_t id;uint32_t padding;
    uintptr_t stack;uint64_t stack_bytes;
} FxZ63TaskScope;
enum { FX_Z63_TASK_EMPTY,FX_Z63_TASK_CREATED,FX_Z63_TASK_RUNNING,FX_Z63_TASK_FAILED };
/* Owned, initially zero scope and 16-byte aligned caller-owned stack. Failed
 * resume retains the ID and stack; no hidden delete/retry or active unload. */
FxResult fx_z63_task_start(FxZ63TaskScope *,void (*entry)(void *),void *context,void *stack,uint32_t stack_bytes);
#endif
