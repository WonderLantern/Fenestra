#ifndef FX_ZF_C301_MODULE_RUNTIME_H
#define FX_ZF_C301_MODULE_RUNTIME_H
#include "module_stage.h"
#include "services.h"
#include "exec_context.h"

enum {
    FX_ZF_MODULE_EMPTY = 0u,
    FX_ZF_MODULE_PREPARED = 1u,
    FX_ZF_MODULE_BOUND = 2u,
    FX_ZF_MODULE_ACTIVE = 3u,
    FX_ZF_MODULE_FAILED = 4u,
    FX_ZF_MODULE_STOPPED = 5u,
    FX_ZF_MODULE_FINISHED = 6u
};

typedef struct {
    FxZfModuleStage stage;
    FxZfServices services;
    FxExecArena *execution;
    FxZfExecContext *exec_context;
    FxServiceModule *user_api;
    FxServiceExchange *user_exchange;
    uintptr_t private_root,user_root,user_stack;
    uint32_t phase,initialized;
    uint64_t stop_deadline;
    FxLease lease;
    uint64_t invocation_deadline;
    FxResult error;
} FxZfModuleRuntime;

FxResult fx_zf_module_runtime_prepare(FxZfModuleRuntime *,const FxZfCardNative *,
    const char *,const char *,uint32_t,const uint8_t[32],uint64_t,
    uint8_t *,uint32_t,uint8_t *,uint32_t,uint8_t *,uint32_t,uint64_t);
FxResult fx_zf_module_runtime_bind(FxZfModuleRuntime *,const char *,uint32_t,uint32_t,uint32_t);
FxResult fx_zf_module_runtime_bind_execution(FxZfModuleRuntime *,FxExecArena *);
FxResult fx_zf_module_runtime_bind_user_view(FxZfModuleRuntime *,FxZfExecContext *,
    FxServiceModule *,FxServiceExchange *,uintptr_t private_root,uintptr_t user_root,uintptr_t user_stack);
FxResult fx_zf_module_runtime_activate(FxZfModuleRuntime *,uint64_t);
FxResult fx_zf_module_runtime_invoke(FxZfModuleRuntime *,uint64_t,int *);
FxResult fx_zf_module_runtime_stop(FxZfModuleRuntime *,uint64_t,uint32_t panic);
#endif
