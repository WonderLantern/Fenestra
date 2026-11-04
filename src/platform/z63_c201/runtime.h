#ifndef FX_Z63_RUNTIME_H
#define FX_Z63_RUNTIME_H
#include "services.h"
#include "module_worker.h"
typedef struct FxZ63Runtime FxZ63Runtime;
void fx_z63_probe_module_start(void);
void fx_z63_runtime_stop(FxZ63Runtime *);
uint32_t fx_z63_runtime_state(const FxZ63Runtime *);
uint32_t fx_z63_runtime_count(const FxZ63Runtime *);
FxZ63ModuleWorker *fx_z63_runtime_worker(const FxZ63Runtime *,uint32_t);
FxPlatformOwner *fx_z63_runtime_owner(const FxZ63Runtime *,uint32_t);
void fx_z63_menu_set_runtime(FxZ63Runtime *);
#endif
