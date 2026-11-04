#ifndef FX_ZF_WORKER_TASK_H
#define FX_ZF_WORKER_TASK_H
#include "task.h"
FxResult fx_zf_worker_task_start(FxZfTaskScope *,void (*)(void *),void *,void *,uint32_t);
FxResult fx_zf_task_delay_ticks(uint32_t);
#endif
