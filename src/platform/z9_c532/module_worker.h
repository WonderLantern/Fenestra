#ifndef FX_Z9_C532_MODULE_WORKER_H
#define FX_Z9_C532_MODULE_WORKER_H
#include "fenestra_services.h"
#include "module_session.h"
#include "task.h"
#include "fenestra_panic.h"
typedef struct FxZ9ModuleWorker FxZ9ModuleWorker;
enum {FX_Z9_WORKER_EMPTY,FX_Z9_WORKER_LOADING,FX_Z9_WORKER_ACTIVE,
      FX_Z9_WORKER_STOPPING,FX_Z9_WORKER_QUIESCED,FX_Z9_WORKER_PANIC,FX_Z9_WORKER_FINISHED};
uint32_t fx_z9_worker_bytes(void);
/* Verified directory entry + native backend, after Activate only. Both allocations
 * come from the normal Nikon allocator and are retained until camera reboot.
 * The backend copies/validates all requests and owns hardware stop/leases.
 * Starting a task is not acceptance of a RAW mode or a completed activation.
 * After initialization starts, failures retain BOTH allocations even when
 * out is still NULL; never free/reuse them following an error. */
FxResult fx_z9_worker_start(void *worker_allocation,uint32_t worker_bytes,
                          void *session_allocation,uint32_t session_bytes,
                          const FxDiscoveredModule *,uint32_t owner,uint32_t generation,
                          uint32_t verified_capabilities,FxServiceBackend,
                          FxZ9ModuleWorker **out);
FxResult fx_z9_worker_action(FxZ9ModuleWorker *,uint32_t action);
/* Native shutdown only: closes admission, waits for backend stop ACK; retains
 * all allocations/unfinished EL0 continuation. No UI Deactivate or unload. */
void fx_z9_worker_shutdown(FxZ9ModuleWorker *);
uint32_t fx_z9_worker_phase(const FxZ9ModuleWorker *);
uint32_t fx_z9_worker_error(const FxZ9ModuleWorker *);
/* Boot-retained copied identity; independent of the discovery snapshot. */
const char *fx_z9_worker_identity(const FxZ9ModuleWorker *);
/* Snapshot is immutable only after the acquire-observed Panic publication. */
FxResult fx_z9_worker_panic_report(const FxZ9ModuleWorker *,FxPanicReport *);
#endif
