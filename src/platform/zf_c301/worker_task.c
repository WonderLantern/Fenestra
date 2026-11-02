#include "worker_task.h"
FxResult fx_zf_worker_task_start(FxZfTaskScope *scope,void (*entry)(void *),void *context,
                                void *stack,uint32_t bytes) {
    const FxZfNativeTaskOps ops={
        (int32_t (*)(const FxZfNativeTaskRequest *))(uintptr_t)0x40185064u,
        (int32_t (*)(int32_t))(uintptr_t)0x401855e0u};
    const FxZfTaskConfig config={entry,context,stack,bytes,28u,1u};
    return fx_zf_task_start(scope,&config,&ops);
}
FxResult fx_zf_task_delay_ticks(uint32_t ticks) {
    if(!ticks || ticks>1000u)return FX_ARGUMENT;
    return ((int32_t (*)(uint32_t))(uintptr_t)0x40187b00u)(ticks)?FX_IO:FX_OK;
}
