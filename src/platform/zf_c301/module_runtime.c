#if !defined(FX_ZF_EXPERIMENTAL_MODULE_RUNTIME) || FX_ZF_EXPERIMENTAL_MODULE_RUNTIME != 1
#error "Zf module runtime is experimental and requires the checked target build"
#endif
#include "module_runtime.h"
#include "fenestra_memory.h"

static void fail(FxZfModuleRuntime *r,FxResult error) {
    if(!r)return;
    r->error=error;
    r->phase=FX_ZF_MODULE_FAILED;
}

typedef struct {FxZfModuleRuntime *runtime;uint64_t now;int result;} FxZfInvoke;
static uint64_t execution_time(uint64_t fallback) {
#if defined(__aarch64__)
    uint64_t count,freq;(void)fallback;
    __asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0":"=r"(count),"=r"(freq));
    if(!freq || freq>UINT32_MAX)return UINT64_MAX;
    return count/freq*1000u+count%freq*1000u/freq;
#else
    return fallback;
#endif
}
static int invoke_user(FxZfExecContext *context) {
    FxZfInvoke *call=(FxZfInvoke *)context->operation_context;
    FxZfModuleRuntime *r=call->runtime;
    /* Arena checks and publication belong to the arena's TTBR0 context. */
    FxResult result=fx_exec_live(r->execution);
    if(result!=FX_OK)return (int)result;
    if(!r->execution->published)return FX_STATE;
    if(execution_time(call->now)>=r->invocation_deadline)return FX_TIMEOUT;
    if(!context->continuation[34]) {
        *r->user_api=r->services.module;
        r->user_api->exchange=r->user_exchange;
        r->user_exchange->reply=r->services.exchange.reply;
    }
    call->result=fx_zf_user_call((uintptr_t)r->stage.loader.image+r->stage.loader.module.entry_offset,
        r->user_api,r->user_root,r->user_stack,context);
    if(context->continuation[34])return FX_INTERRUPTED;
    r->services.exchange.request=r->user_exchange->request;
    return FX_OK;
}

static int activate_arena(FxZfExecContext *context) {
    FxZfInvoke *call=(FxZfInvoke *)context->operation_context;
    FxZfModuleRuntime *r=call->runtime;
    FxResult result=fx_exec_live(r->execution);
    if(result==FX_OK)result=fx_loader_activate_mapped(&r->stage.loader,r->execution,call->now);
    return (int)result;
}

static int confirm_quiesced(FxZfExecContext *context) {
    FxZfInvoke *call=(FxZfInvoke *)context->operation_context;
    FxLoader *loader=&call->runtime->stage.loader;
    return (int)fx_loader_confirm_quiesced(loader,loader->runtime.generation,call->now);
}

static FxResult invoke_event(FxZfModuleRuntime *r,uint64_t now,uint32_t event,uint32_t value,
                            int *module_result) {
    /* Card IO and private table construction may precede INIT on this worker.
     * Start the invocation budget at entry, not at that earlier loop clock. */
    now=execution_time(now);
    if(now==UINT64_MAX)return FX_CAPABILITY;
    uint32_t pending=r->exec_context && r->exec_context->continuation[34];
    FxResult result=pending?FX_OK:fx_zf_services_prepare(&r->services,now,event,value);
    if(result==FX_OK && (!r->execution || !r->execution->bound || !r->exec_context ||
       !r->user_api || !r->private_root || !r->user_root || !r->user_stack))result=FX_CAPABILITY;
    FxRuntime *runtime=&r->stage.loader.runtime;
    if(result==FX_OK && !pending) {
        if(now>UINT64_MAX-100u)return FX_BOUNDS;
        result=fx_execution_resume(runtime,now);
        if(result==FX_OK)result=fx_acquire(runtime,r->services.module.generation,now,&r->lease);
        r->invocation_deadline=now+100u;
    }
    FxZfInvoke call={r,now,0};
    if(result==FX_OK) {
        r->exec_context->operation_context=(uintptr_t)&call;
        result=(FxResult)fx_zf_exec_call(r->exec_context,r->private_root,invoke_user);
        r->exec_context->operation_context=0u;
        if(result==FX_INTERRUPTED)return result;
        if(result==FX_OK && r->exec_context->diagnostics.faults)result=FX_INTEGRITY;
        if(result==FX_OK)*module_result=call.result;
        uint64_t ended=execution_time(now);
        if(result==FX_OK && ended>=r->invocation_deadline)result=FX_TIMEOUT;
        FxResult released=fx_release(runtime,r->lease,ended);
        if(released==FX_OK)r->lease=(FxLease){0};
        if(result==FX_OK)result=released;
        if(result==FX_OK)result=fx_execution_idle(runtime,ended);
    }
    /* Match the Z9 worker: a nonzero entry result is a module failure. Do
     * not dispatch a request left behind by the failing invocation. The
     * execution lease has already been released before reporting it. */
    if(result==FX_OK && call.result)result=FX_INTEGRITY;
    if(result==FX_OK) {
        FxResult dispatched=fx_zf_services_dispatch_pending(&r->services);
        if(dispatched!=FX_OK && dispatched!=FX_EMPTY)result=dispatched;
    }
    return result;
}

FxResult fx_zf_module_runtime_prepare(FxZfModuleRuntime *r,const FxZfCardNative *native,
    const char *native_path,const char *logical_path,uint32_t bytes,const uint8_t digest[32],
    uint64_t capabilities,uint8_t *file,uint32_t file_capacity,uint8_t *package,
    uint32_t package_capacity,uint8_t *image,uint32_t image_capacity,uint64_t now) {
    if(!r || !native || !native_path || !logical_path || !digest || !file || !package || !image)
        return FX_ARGUMENT;
    if(r->phase!=FX_ZF_MODULE_EMPTY)return FX_BUSY;
    FxResult result=fx_zf_module_stage(&r->stage,native,native_path,logical_path,bytes,digest,
        capabilities,file,file_capacity,package,package_capacity,image,image_capacity,now);
    if(result==FX_OK && r->stage.loader.module.entry_abi!=FX_MODULE_ENTRY_SERVICES)
        result=FX_CAPABILITY;
    if(result!=FX_OK){fail(r,result);return result;}
    r->phase=FX_ZF_MODULE_PREPARED;r->error=FX_OK;return FX_OK;
}

FxResult fx_zf_module_runtime_bind(FxZfModuleRuntime *r,const char *id,uint32_t owner,
    uint32_t generation,uint32_t capabilities) {
    if(!r || !id || !owner || !generation)return FX_ARGUMENT;
    if(r->phase!=FX_ZF_MODULE_PREPARED)return FX_STATE;
    FxResult result=fx_zf_services_init(&r->services,owner,generation,capabilities);
    if(result==FX_OK)result=fx_zf_services_bind_module(&r->services,id,
        &r->stage.loader.module,r->stage.loader.image,r->stage.loader.module.bytes);
    if(result!=FX_OK){fail(r,result);return result;}
    r->phase=FX_ZF_MODULE_BOUND;r->error=FX_OK;return FX_OK;
}

FxResult fx_zf_module_runtime_activate(FxZfModuleRuntime *r,uint64_t now) {
    if(!r || r->phase!=FX_ZF_MODULE_BOUND)return FX_STATE;
    if(!r->execution || !r->execution->bound || !r->exec_context ||
       !r->user_api || !r->private_root || !r->user_root || !r->user_stack)return FX_CAPABILITY;
    int module_result=0;
    FxResult result=FX_OK;
    if(!r->execution->published) {
        FxZfInvoke call={r,now,0};
        r->exec_context->operation_context=(uintptr_t)&call;
        result=fx_zf_exec_call(r->exec_context,r->private_root,activate_arena);
        r->exec_context->operation_context=0u;
    }
    if(result==FX_OK)result=invoke_event(r,now,FX_EVENT_INIT,0u,&module_result);
    if(result==FX_INTERRUPTED)return result;
    if(result!=FX_OK){fail(r,result);return result;}
    r->initialized=1u;
    r->phase=r->services.platform.phase==FX_OWNER_FINISHED?FX_ZF_MODULE_FINISHED:FX_ZF_MODULE_ACTIVE;
    r->error=FX_OK;return FX_OK;
}

FxResult fx_zf_module_runtime_bind_execution(FxZfModuleRuntime *r,FxExecArena *execution) {
    if(!r || !execution)return FX_ARGUMENT;
    if(r->phase!=FX_ZF_MODULE_BOUND || r->execution)return FX_STATE;
    if(!execution->bound || execution->published)return FX_CAPABILITY;
    FxResult result=fx_exec_live(execution);
    if(result!=FX_OK)return result;
    r->execution=execution;
    return FX_OK;
}

FxResult fx_zf_module_runtime_bind_user_view(FxZfModuleRuntime *r,FxZfExecContext *context,
    FxServiceModule *api,FxServiceExchange *exchange,uintptr_t private_root,uintptr_t user_root,uintptr_t user_stack) {
    if(!r || !context || !api || !exchange || !private_root || !user_root || !user_stack ||
       (private_root&4095u) || (user_root&4095u) || (user_stack&15u))return FX_ARGUMENT;
    uintptr_t owner_begin=(uintptr_t)r;
    uintptr_t owner_end=owner_begin+sizeof(*r);
    uintptr_t api_begin=(uintptr_t)api,exchange_begin=(uintptr_t)exchange;
    if(owner_end<owner_begin || api_begin>UINTPTR_MAX-sizeof(*api) ||
       exchange_begin>UINTPTR_MAX-sizeof(*exchange) ||
       (api_begin<owner_end && owner_begin<api_begin+sizeof(*api)) ||
       (exchange_begin<owner_end && owner_begin<exchange_begin+sizeof(*exchange)))return FX_CAPABILITY;
    if(r->phase!=FX_ZF_MODULE_BOUND || !r->execution || r->exec_context)return FX_STATE;
    if(!r->execution->bound)return FX_CAPABILITY;
    context->simd_enabled=(r->stage.loader.module.required_capabilities&FX_SDK_SIMD)?1u:0u;
    r->exec_context=context;r->user_api=api;r->user_exchange=exchange;r->private_root=private_root;
    r->user_root=user_root;r->user_stack=user_stack;
    return FX_OK;
}

FxResult fx_zf_module_runtime_invoke(FxZfModuleRuntime *r,uint64_t now,int *module_result) {
    if(!r || !module_result)return FX_ARGUMENT;
    if(r->phase!=FX_ZF_MODULE_ACTIVE)return FX_STATE;
    if(!r->execution || !r->execution->bound || !r->execution->published)return FX_CAPABILITY;
    FxResult result=invoke_event(r,now,FX_EVENT_TICK,0u,module_result);
    if(result!=FX_OK && result!=FX_INTERRUPTED)fail(r,result);
    else if(r->services.platform.phase==FX_OWNER_FINISHED)r->phase=FX_ZF_MODULE_FINISHED;
    return result;
}

FxResult fx_zf_module_runtime_stop(FxZfModuleRuntime *r,uint64_t now,uint32_t panic) {
    if(!r)return FX_ARGUMENT;
    if(r->phase==FX_ZF_MODULE_EMPTY || r->phase==FX_ZF_MODULE_STOPPED)return FX_OK;
    /* Keep the owner alive while native UI/resources drain. A STOP notice
     * never starts a new module or abandons a BUSY resource after one poll. */
    if(!r->stop_deadline) {
        if(now>UINT64_MAX-2000u)return FX_BOUNDS;
        r->stop_deadline=now+2000u;
    }
    if(now>=r->stop_deadline)return FX_TIMEOUT;
    if(r->phase==FX_ZF_MODULE_FAILED)panic=1u;
    /* IRQ unwound the private gate before Nikon handles its pending IRQ.
     * On shutdown abandon that saved EL0 continuation, as Z9 closes its
     * session; never resume an interrupted entry just to deliver STOP. */
    if(r->exec_context && r->exec_context->continuation[34]) {
        r->exec_context->continuation[34]=0u;r->initialized=0u;
    }
    if(r->lease.id) {
        FxResult released=fx_release(&r->stage.loader.runtime,r->lease,now);
        /* fx_release records a late-runtime panic before touching its lease.
         * Preserve the token so the next cleanup step can release it there. */
        if(released!=FX_OK)return released;
        r->lease=(FxLease){0};r->initialized=0u;
        if(r->stage.loader.runtime.phase==FX_ACTIVE) {
            released=fx_execution_idle(&r->stage.loader.runtime,now);
            if(released!=FX_OK)return released;
        }
    }
    if(r->phase==FX_ZF_MODULE_ACTIVE && r->initialized) {
        int module_result=0;
        FxResult stopped=invoke_event(r,now,FX_EVENT_STOP,panic,&module_result);
        if(stopped!=FX_OK) {
            if(stopped!=FX_BUSY && stopped!=FX_INTERRUPTED)fail(r,stopped);
            return stopped==FX_INTERRUPTED?FX_BUSY:stopped;
        }
        r->initialized=0u;
    }
    FxResult result=fx_zf_services_stop(&r->services,panic);
    if(result!=FX_OK) {
        if(result!=FX_BUSY)fail(r,result);
        return result;
    }
    now=execution_time(now);
    if(now==UINT64_MAX)return FX_CAPABILITY;
    if(r->stage.loader.runtime.phase==FX_ACTIVE ||
       r->stage.loader.runtime.phase==FX_PREPARED ||
       r->stage.loader.runtime.phase==FX_VALIDATING) {
        result=fx_disable(&r->stage.loader.runtime,now,10000u);
    }
    if(result==FX_OK && r->stage.loader.runtime.phase==FX_QUIESCING) {
        if(r->exec_context && r->private_root) {
            FxZfInvoke call={r,now,0};
            r->exec_context->operation_context=(uintptr_t)&call;
            result=fx_zf_exec_call(r->exec_context,r->private_root,confirm_quiesced);
            r->exec_context->operation_context=0u;
        } else result=fx_loader_confirm_quiesced(&r->stage.loader,r->stage.loader.runtime.generation,now);
    }
    if(result!=FX_OK) {
        if(result!=FX_BUSY)fail(r,result);
        return result;
    }
    r->initialized=0u;r->phase=FX_ZF_MODULE_STOPPED;return FX_OK;
}
