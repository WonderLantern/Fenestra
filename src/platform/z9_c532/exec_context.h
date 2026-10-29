#ifndef FX_Z9_C532_EXEC_CONTEXT_H
#define FX_Z9_C532_EXEC_CONTEXT_H
#include "fenestra.h"
#include "fenestra_simd.h"
/* Architecture context only. No Nikon task frame or native vectors are edited.
 * The platform owner must supply owned/validated private translation tables.
 * Hardware quiesce runs AFTER the gate restores the original native context. */
typedef struct {
    uint32_t magic,version,phase,error,command;int32_t task_id;
    uint32_t frame,invocations,locked,targets;
    uint64_t pool;uint32_t faults,reserved;uint64_t esr,elr,far;
    uint32_t context_checks,context_errors;uint64_t before[9],after[9];
} FxZ9ExecDiagnostics;
typedef struct FxZ9ExecContext {
    FxZ9ExecDiagnostics diagnostics;
    uintptr_t trap_anchor,user_anchor;uint64_t last_spsr,last_elr;
    uint32_t user_entries,user_exits;uint64_t continuation[36];
    uint64_t simd_enabled,simd_reserved;
    _Alignas(16) FxSimdState module_simd;
    uint64_t before_control[2],after_control[2];
} FxZ9ExecContext;
_Static_assert(sizeof(FxZ9ExecDiagnostics)==232,"private gate diagnostic prefix");
_Static_assert(offsetof(FxZ9ExecContext,trap_anchor)==232 && offsetof(FxZ9ExecContext,user_anchor)==240,"trusted gate anchors");
_Static_assert(offsetof(FxZ9ExecContext,last_spsr)==248 && offsetof(FxZ9ExecContext,last_elr)==256,"exception state");
_Static_assert(offsetof(FxZ9ExecContext,user_entries)==264 && offsetof(FxZ9ExecContext,user_exits)==268,"EL0 invocation counters");
_Static_assert(offsetof(FxZ9ExecContext,continuation)==272,"interrupt continuation");
_Static_assert(offsetof(FxZ9ExecContext,simd_enabled)==FX_MODULE_SIMD_ENABLE_OFFSET,"SIMD capability offset");
_Static_assert(offsetof(FxZ9ExecContext,module_simd)==FX_MODULE_SIMD_OFFSET,"SIMD continuation offset");
FxResult fx_z9_exec_call(FxZ9ExecContext *,uintptr_t owned_root,int (*operation)(FxZ9ExecContext *));
void fx_z9_private_fault(FxZ9ExecContext *,uint64_t esr,uint64_t elr,uint64_t far,uint64_t argument);
int fx_z9_user_call(uintptr_t entry,const void *api,uintptr_t owned_user_root,uintptr_t stack,FxZ9ExecContext *);
extern const uint8_t fx_z9_user_return[];
#endif
