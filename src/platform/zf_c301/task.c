#if !defined(FX_ZF_C301_EXPERIMENTAL_TASK) || FX_ZF_C301_EXPERIMENTAL_TASK != 1
#error "Zf task adapter is experimental and is not enabled in a camera build"
#endif
#include "task.h"
#include "fenestra_module.h"

_Static_assert(sizeof(uintptr_t)==8u,"Zf task ABI requires a 64-bit host/target");
_Static_assert(sizeof(FxZfNativeTaskRequest)==56u,"Zf native task request size");
_Static_assert(offsetof(FxZfNativeTaskRequest,argument)==8u &&
               offsetof(FxZfNativeTaskRequest,entry)==16u &&
               offsetof(FxZfNativeTaskRequest,priority)==24u &&
               offsetof(FxZfNativeTaskRequest,stack_bytes)==32u &&
               offsetof(FxZfNativeTaskRequest,stack)==40u &&
               offsetof(FxZfNativeTaskRequest,native_mode)==48u &&
               offsetof(FxZfNativeTaskRequest,affinity)==52u,"Zf native task request offsets");

static int overlaps(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    if(a>UINTPTR_MAX-an || b>UINTPTR_MAX-bn)return 1;
    return a<b+bn && b<a+an;
}

FxResult fx_zf_task_start(FxZfTaskScope *scope,const FxZfTaskConfig *config,const FxZfNativeTaskOps *ops) {
    if(!scope || !config || !ops)return FX_ARGUMENT;
    uintptr_t scope_at=(uintptr_t)scope,config_at=(uintptr_t)config,ops_at=(uintptr_t)ops;
    if(scope_at%_Alignof(FxZfTaskScope) || config_at%_Alignof(FxZfTaskConfig) ||
       ops_at%_Alignof(FxZfNativeTaskOps))return FX_ARGUMENT;
    if(overlaps(scope_at,sizeof(*scope),config_at,sizeof(*config)) ||
       overlaps(scope_at,sizeof(*scope),ops_at,sizeof(*ops)) ||
       overlaps(config_at,sizeof(*config),ops_at,sizeof(*ops)))return FX_ARGUMENT;
    FxZfTaskConfig chosen=*config;
    FxZfNativeTaskOps backend=*ops;
    uintptr_t stack_at=(uintptr_t)chosen.stack,entry_at=(uintptr_t)chosen.entry;
    if(!backend.create || !backend.start || ((uintptr_t)backend.create&3u) || ((uintptr_t)backend.start&3u) ||
       !chosen.entry || !chosen.stack ||
       (entry_at&3u) || (stack_at&15u) ||
       chosen.stack_bytes<4096u || chosen.stack_bytes>FX_MODULE_STACK_MAX || (chosen.stack_bytes&15u) ||
       stack_at>UINTPTR_MAX-chosen.stack_bytes ||
       !chosen.priority || chosen.priority>256u || chosen.native_mode<1u || chosen.native_mode>3u)
        return FX_ARGUMENT;
    if(overlaps(stack_at,chosen.stack_bytes,scope_at,sizeof(*scope)) ||
       overlaps(stack_at,chosen.stack_bytes,config_at,sizeof(*config)) ||
       overlaps(stack_at,chosen.stack_bytes,ops_at,sizeof(*ops)) ||
       overlaps(stack_at,chosen.stack_bytes,(uintptr_t)backend.create,4u) ||
       overlaps(stack_at,chosen.stack_bytes,(uintptr_t)backend.start,4u) ||
       overlaps(stack_at,chosen.stack_bytes,entry_at,1u) ||
       (chosen.argument && overlaps(stack_at,chosen.stack_bytes,(uintptr_t)chosen.argument,1u)))
        return FX_ARGUMENT;
    if(scope->phase || scope->id || scope->start_result || scope->reserved || scope->stack || scope->stack_bytes)
        return FX_BUSY;
    FxZfNativeTaskRequest request={0u,0u,(uintptr_t)chosen.argument,entry_at,
        chosen.priority,0u,chosen.stack_bytes,stack_at,chosen.native_mode,UINT32_MAX};
    scope->stack=stack_at;
    scope->stack_bytes=chosen.stack_bytes;
    scope->id=backend.create(&request);
    if(scope->id<1) {
        scope->phase=FX_ZF_TASK_CREATE_FAILED;
        return FX_IO;
    }
    scope->phase=FX_ZF_TASK_CREATED;
    scope->start_result=backend.start(scope->id);
    if(scope->start_result) {
        scope->phase=FX_ZF_TASK_START_FAILED;
        return FX_IO;
    }
    scope->phase=FX_ZF_TASK_START_ACCEPTED;
    return FX_OK;
}
