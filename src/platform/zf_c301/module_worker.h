#ifndef FX_ZF_C301_MODULE_WORKER_H
#define FX_ZF_C301_MODULE_WORKER_H
#include "fenestra_services.h"
#include "module_session.h"
#include "worker_task.h"
#include "fenestra_panic.h"
typedef struct FxZfModuleWorker FxZfModuleWorker;
enum {FX_ZF_WORKER_EMPTY,FX_ZF_WORKER_LOADING,FX_ZF_WORKER_ACTIVE,
      FX_ZF_WORKER_STOPPING,FX_ZF_WORKER_QUIESCED,FX_ZF_WORKER_PANIC,FX_ZF_WORKER_FINISHED};
uint32_t fx_zf_worker_bytes(void);
/* Verified directory entry + native backend, after Activate only. Both allocations
 * come from the normal Nikon allocator and are retained until camera reboot.
 * The backend copies/validates all requests and owns hardware stop/leases.
 * Starting a task is not acceptance of a RAW mode or a completed activation.
 * After initialization starts, failures retain BOTH allocations even when
 * out is still NULL; never free/reuse them following an error. */
FxResult fx_zf_worker_start(void *worker_allocation,uint32_t worker_bytes,
                          void *session_allocation,uint32_t session_bytes,
                          const FxDiscoveredModule *,uint32_t owner,uint32_t generation,
                          uint32_t verified_capabilities,FxServiceBackend,
                          FxZfModuleWorker **out);
FxResult fx_zf_worker_action(FxZfModuleWorker *,uint32_t action);
/* Native shutdown only: closes admission, waits for backend stop ACK; retains
 * all allocations/unfinished EL0 continuation. No UI Deactivate or unload. */
void fx_zf_worker_shutdown(FxZfModuleWorker *);
uint32_t fx_zf_worker_phase(const FxZfModuleWorker *);
uint32_t fx_zf_worker_error(const FxZfModuleWorker *);
/* Boot-retained copied identity; independent of the discovery snapshot. */
const char *fx_zf_worker_identity(const FxZfModuleWorker *);
/* Snapshot is immutable only after the acquire-observed Panic publication. */
FxResult fx_zf_worker_panic_report(const FxZfModuleWorker *,FxPanicReport *);
#endif
