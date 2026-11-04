#ifndef FX_ZF_C301_EXEC_CONTEXT_H
#define FX_ZF_C301_EXEC_CONTEXT_H
#include "fenestra_simd.h"
#include "fenestra.h"

typedef struct {
    uint32_t magic,version,phase,error,command;
    int32_t task_id;
    uint32_t frame,invocations,locked,targets;
    uint64_t pool;
    uint32_t faults,reserved;
    uint64_t esr,elr,far;
    uint32_t context_checks,context_errors;
    uint64_t before[9],after[9];
} FxZfExecDiagnostics;

typedef struct {
    FxZfExecDiagnostics diagnostics;
    uintptr_t trap_anchor,user_anchor;
    uint64_t last_spsr,last_elr;
    uint32_t user_entries,user_exits;
    uint64_t continuation[36];
    uint64_t simd_enabled,simd_reserved;
    _Alignas(16) FxSimdState simd;
    uint64_t before_control[2],after_control[2];
    uintptr_t operation_context;
} FxZfExecContext;

_Static_assert(sizeof(FxZfExecDiagnostics)==232,"Zf private gate diagnostics");
_Static_assert(offsetof(FxZfExecContext,trap_anchor)==232 &&
               offsetof(FxZfExecContext,user_anchor)==240,
               "Zf private gate anchors");
_Static_assert(offsetof(FxZfExecContext,continuation)==272,
               "Zf private gate continuation");
_Static_assert(offsetof(FxZfExecContext,simd_enabled)==FX_MODULE_SIMD_ENABLE_OFFSET,
               "Zf private gate SIMD capability offset");
_Static_assert(offsetof(FxZfExecContext,simd)==FX_MODULE_SIMD_OFFSET,
               "Zf private gate SIMD state offset");

FxResult fx_zf_exec_call(FxZfExecContext *,uintptr_t root,int (*operation)(FxZfExecContext *));
void fx_zf_private_fault(FxZfExecContext *,uint64_t esr,uint64_t elr,uint64_t far);
int fx_zf_user_call(uintptr_t entry,const void *api,uintptr_t root,uintptr_t stack,FxZfExecContext *);
extern const uint8_t fx_zf_user_return[];
#endif
