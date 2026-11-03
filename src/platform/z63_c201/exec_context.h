#ifndef FX_Z63_C201_EXEC_CONTEXT_H
#define FX_Z63_C201_EXEC_CONTEXT_H
#include "fenestra.h"
#include "fenestra_simd.h"
#include "exec_time.h"
/* Architecture context only. No Nikon task frame or native vectors are edited.
 * The platform owner must supply owned/validated private translation tables.
 * Hardware quiesce runs AFTER the gate restores the original native context. */
typedef struct {
    uint32_t magic,version,phase,error,command;int32_t task_id;
    uint32_t frame,invocations,locked,targets;
    uint64_t pool;uint32_t faults,reserved;uint64_t esr,elr,far;
    uint32_t context_checks,context_errors;uint64_t before[9],after[9];
} FxZ63ExecDiagnostics;
typedef struct FxZ63ExecContext {
    FxZ63ExecDiagnostics diagnostics;
    uintptr_t trap_anchor,user_anchor;uint64_t last_spsr,last_elr;
    uint32_t user_entries,user_exits;uint64_t continuation[36];
    uint64_t simd_enabled,simd_reserved;
    _Alignas(16) FxSimdState module_simd;
    uint64_t before_control[2],after_control[2];
    uint64_t active_start,active_ticks,active_limit;
} FxZ63ExecContext;
_Static_assert(sizeof(FxZ63ExecDiagnostics)==232,"private gate diagnostic prefix");
_Static_assert(offsetof(FxZ63ExecContext,trap_anchor)==232 && offsetof(FxZ63ExecContext,user_anchor)==240,"trusted gate anchors");
_Static_assert(offsetof(FxZ63ExecContext,last_spsr)==248 && offsetof(FxZ63ExecContext,last_elr)==256,"exception state");
_Static_assert(offsetof(FxZ63ExecContext,user_entries)==264 && offsetof(FxZ63ExecContext,user_exits)==268,"EL0 invocation counters");
_Static_assert(offsetof(FxZ63ExecContext,continuation)==272,"interrupt continuation");
_Static_assert(offsetof(FxZ63ExecContext,simd_enabled)==FX_MODULE_SIMD_ENABLE_OFFSET,"SIMD capability offset");
_Static_assert(offsetof(FxZ63ExecContext,module_simd)==FX_MODULE_SIMD_OFFSET,"SIMD continuation offset");
_Static_assert(offsetof(FxZ63ExecContext,active_start)==FX_Z63_ACTIVE_START &&
    offsetof(FxZ63ExecContext,active_ticks)==FX_Z63_ACTIVE_TICKS &&
    offsetof(FxZ63ExecContext,active_limit)==FX_Z63_ACTIVE_LIMIT,"EL0 counter accounting offsets");
FxResult fx_z63_exec_call(FxZ63ExecContext *,uintptr_t owned_root,int (*operation)(FxZ63ExecContext *));
void fx_z63_private_fault(FxZ63ExecContext *,uint64_t esr,uint64_t elr,uint64_t far,uint64_t argument);
int fx_z63_user_call(uintptr_t entry,const void *api,uintptr_t owned_user_root,uintptr_t stack,FxZ63ExecContext *);
extern const uint8_t fx_z63_user_return[];
#endif
