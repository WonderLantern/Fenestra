#ifndef FX_Z8_C311_MODULE_SESSION_H
#define FX_Z8_C311_MODULE_SESSION_H
#include "module_stage.h"
#include "address_space.h"
#include "exec_context.h"
#include "fenestra_api.h"
#include "fenestra_exec.h"
#include "fenestra_accounting.h"
/* v1/v2 wrappers retain 64KiB stacks. Explicit v3 needs the same stack
 * declaration at planning and init; the loaded package is checked again. */
FxResult fx_z8_session_layout(uint32_t package_bytes,uint32_t image_bytes,FxExecLayout *);
FxResult fx_z8_session_layout_stacks(uint32_t package_bytes,uint32_t image_bytes,FxModuleStacks,FxExecLayout *);
typedef struct FxZ8ModuleSession FxZ8ModuleSession;
FxResult fx_z8_session_account(FxZ8ModuleSession *,FxAccountRow *);
FxResult fx_z8_session_file_observer(FxZ8ModuleSession *,FxFileObserver);
/* The caller owns this normal Nikon heap allocation and the native task that
 * uses it. Init/load/invoke are serialized by that worker, after Activate.
 * No native task, translation table or exception vector is edited by init.
 * The original task stack MUST be the returned kernel_stack/stack_bytes.
 * Normal file IO uses original tables. Only validated private execution uses
 * the owned views. No module-supplied pointer may reach a native GUI/service.
 * Load scratch is separate and released on original tables before private
 * publication; uncertain cleanup retains it. Close retains the execution
 * allocation/image/continuation ownership until reboot. */
typedef struct {
    void *api,*exchange,*kernel_stack;uint32_t exchange_bytes,stack_bytes;
    const FxModule *module;void *image;uint32_t image_bytes;
} FxZ8SessionBuffers;
FxResult fx_z8_session_init(void *allocation,uint32_t bytes,uint32_t package_bytes,uint32_t image_bytes,
                             FxZ8ModuleSession **,FxZ8SessionBuffers *);
FxResult fx_z8_session_init_stacks(void *allocation,uint32_t bytes,uint32_t package_bytes,uint32_t image_bytes,
                             FxModuleStacks,FxZ8ModuleSession **,FxZ8SessionBuffers *);
FxResult fx_z8_session_load(FxZ8ModuleSession *,const char *native_path,const char *logical_path,
                           uint32_t module_bytes,const uint8_t digest[32],uint64_t capabilities);
/* Caller writes the fixed read-only API page only before a NEW invocation.
 * Pointers inside it may address the fixed exchange page; other user pointers
 * fault. INTERRUPTED means retain API/exchange and yield on ORIGINAL tables,
 * then invoke again. A native monotonic deadline applies across IRQ resumes.
 * Backend services/rendering run only after a completed return, on original
 * tables, after validating/copying fixed exchange storage. */
FxResult fx_z8_session_invoke(FxZ8ModuleSession *,uint32_t budget_ms,int *module_result);
int fx_z8_session_pending(const FxZ8ModuleSession *);
FxResult fx_z8_session_close(FxZ8ModuleSession *);
const FxZ8ExecDiagnostics *fx_z8_session_diagnostics(const FxZ8ModuleSession *);
#endif
