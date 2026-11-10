#ifndef FX_Z63_C201_MODULE_WORKER_H
#define FX_Z63_C201_MODULE_WORKER_H
#include "fenestra_services.h"
#include "module_session.h"
#include "task.h"
#include "fenestra_panic.h"
typedef struct FxZ63ModuleWorker FxZ63ModuleWorker;
enum {FX_Z63_WORKER_EMPTY,FX_Z63_WORKER_LOADING,FX_Z63_WORKER_ACTIVE,
      FX_Z63_WORKER_STOPPING,FX_Z63_WORKER_QUIESCED,FX_Z63_WORKER_PANIC,FX_Z63_WORKER_FINISHED};
uint32_t fx_z63_worker_bytes(void);
/* Verified directory entry + native backend, after Activate only. Both allocations
 * come from the normal Nikon allocator and are retained until camera reboot.
 * The backend copies/validates all requests and owns hardware stop/leases.
 * Starting a task is not acceptance of a RAW mode or a completed activation.
 * After initialization starts, failures retain BOTH allocations even when
 * out is still NULL; never free/reuse them following an error. */
FxResult fx_z63_worker_start(void *worker_allocation,uint32_t worker_bytes,
                          void *session_allocation,uint32_t session_bytes,
                          const FxDiscoveredModule *,uint32_t owner,uint32_t generation,
                          uint32_t verified_capabilities,FxServiceBackend,
                          FxZ63ModuleWorker **out);
FxResult fx_z63_worker_action(FxZ63ModuleWorker *,uint32_t action);
/* Native shutdown only: closes admission, waits for backend stop ACK; retains
 * all allocations/unfinished EL0 continuation. No UI Deactivate or unload. */
void fx_z63_worker_shutdown(FxZ63ModuleWorker *);
uint32_t fx_z63_worker_phase(const FxZ63ModuleWorker *);
uint32_t fx_z63_worker_error(const FxZ63ModuleWorker *);
/* Boot-retained copied identity; independent of the discovery snapshot. */
const char *fx_z63_worker_identity(const FxZ63ModuleWorker *);
/* Snapshot is immutable only after the acquire-observed Panic publication. */
FxResult fx_z63_worker_panic_report(const FxZ63ModuleWorker *,FxPanicReport *);
#endif
