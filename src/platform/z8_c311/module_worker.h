#ifndef FX_Z8_C311_MODULE_WORKER_H
#define FX_Z8_C311_MODULE_WORKER_H
#include "fenestra_services.h"
#include "module_session.h"
#include "task.h"
#include "fenestra_panic.h"
typedef struct FxZ8ModuleWorker FxZ8ModuleWorker;
enum {FX_Z8_WORKER_EMPTY,FX_Z8_WORKER_LOADING,FX_Z8_WORKER_ACTIVE,
      FX_Z8_WORKER_STOPPING,FX_Z8_WORKER_QUIESCED,FX_Z8_WORKER_PANIC,FX_Z8_WORKER_FINISHED};
uint32_t fx_z8_worker_bytes(void);
/* Verified directory entry + native backend, after Activate only. Both allocations
 * come from the normal Nikon allocator and are retained until camera reboot.
 * The backend copies/validates all requests and owns hardware stop/leases.
 * Starting a task is not acceptance of a RAW mode or a completed activation.
 * After initialization starts, failures retain BOTH allocations even when
 * out is still NULL; never free/reuse them following an error. */
FxResult fx_z8_worker_start(void *worker_allocation,uint32_t worker_bytes,
                          void *session_allocation,uint32_t session_bytes,
                          const FxDiscoveredModule *,uint32_t owner,uint32_t generation,
                          uint32_t verified_capabilities,FxServiceBackend,
                          FxZ8ModuleWorker **out);
FxResult fx_z8_worker_action(FxZ8ModuleWorker *,uint32_t action);
/* Native shutdown only: closes admission, waits for backend stop ACK; retains
 * all allocations/unfinished EL0 continuation. No UI Deactivate or unload. */
void fx_z8_worker_shutdown(FxZ8ModuleWorker *);
uint32_t fx_z8_worker_phase(const FxZ8ModuleWorker *);
uint32_t fx_z8_worker_error(const FxZ8ModuleWorker *);
/* Boot-retained copied identity; independent of the discovery snapshot. */
const char *fx_z8_worker_identity(const FxZ8ModuleWorker *);
/* Snapshot is immutable only after the acquire-observed Panic publication. */
FxResult fx_z8_worker_panic_report(const FxZ8ModuleWorker *,FxPanicReport *);
#endif
