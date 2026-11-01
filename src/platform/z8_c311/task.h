#ifndef FX_Z8_C311_TASK_H
#define FX_Z8_C311_TASK_H
#include "fenestra.h"
typedef struct {
    uint32_t magic,phase,native_error,reserved;
    int32_t id;uint32_t padding;
    uintptr_t stack;uint64_t stack_bytes;
} FxZ8TaskScope;
enum { FX_Z8_TASK_EMPTY,FX_Z8_TASK_CREATED,FX_Z8_TASK_RUNNING,FX_Z8_TASK_FAILED };
/* Owned, initially zero scope and 16-byte aligned caller-owned stack. Failed
 * resume retains the ID and stack; no hidden delete/retry or active unload. */
FxResult fx_z8_task_start(FxZ8TaskScope *,void (*entry)(void *),void *context,void *stack,uint32_t stack_bytes);
/* Native unit conversion remains separate from the module monotonic clock. */
FxResult fx_z8_task_delay_ticks(uint32_t ticks);
#endif
