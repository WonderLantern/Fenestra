/* Explicit emulator bring-up: one pinned headless card module, no discovery. */
#include "module_session.h"
#include "task.h"
#include "fenestra_services.h"
#include "fenestra_memory.h"
#include "z63_probe_module.h"
typedef struct {FxZ63ModuleSession *session;FxZ63SessionBuffers buffers;FxZ63TaskScope task;} Owner;
__attribute__((noinline)) void fx_z63_probe_module_result(uint32_t stage,uint32_t result,uint64_t detail) {
    __asm__ volatile(""::"r"(stage),"r"(result),"r"(detail):"memory");
}
static void run(void *argument) {
    Owner *s=argument;
    FxResult r=fx_z63_session_load(s->session,"A:\\FENESTRA\\demo-z63.wlm","FENESTRA/demo-z63.wlm",FX_Z63_PROBE_BYTES,fx_z63_probe_digest,0u);
    fx_z63_probe_module_result(10u,(uint32_t)r,fx_z63_session_source_error(s->session));
    if(r!=FX_OK)return;
    const uint32_t events[]={FX_EVENT_INIT,FX_EVENT_TICK,FX_EVENT_STOP};
    for(uint32_t i=0;i<3u;++i) {
        FxServiceModule *api=s->buffers.api;FxServiceExchange *exchange=s->buffers.exchange;
        memset(api,0,sizeof(*api));memset(exchange,0,sizeof(*exchange));
        api->base=(FxModuleServices){FX_ABI,sizeof(*api),0,0};
        api->service_abi=FX_SERVICE_ABI;api->owner=1u;api->generation=1u;
        api->exchange=exchange;api->event=events[i];
        int result=-1;
        for(uint32_t attempts=0;attempts<100u;++attempts) {
            r=fx_z63_session_invoke(s->session,100u,&result);
            if(r!=FX_INTERRUPTED)break;
        }
        fx_z63_probe_module_result(20u+i,(uint32_t)r,((uint64_t)(uint32_t)result<<32)|exchange->request.sequence);
        if(r!=FX_OK) {
            const FxZ63ExecDiagnostics *d=fx_z63_session_diagnostics(s->session);
            fx_z63_probe_module_result(40u,(uint32_t)r,d?((uint64_t)d->faults<<32)|d->context_errors:UINT64_MAX);
            if(d)fx_z63_probe_module_result(41u,(uint32_t)d->esr,d->elr);
            return;
        }
    }
    r=fx_z63_session_close(s->session);
    const FxZ63ExecDiagnostics *d=fx_z63_session_diagnostics(s->session);
    fx_z63_probe_module_result(30u,(uint32_t)r,d?((uint64_t)d->faults<<32)|d->context_errors:UINT64_MAX);
}
void fx_z63_probe_module_start(void) {
    FxExecLayout layout;FxResult r=fx_z63_session_layout(FX_Z63_PROBE_BYTES,65536u,&layout);
    if(r!=FX_OK){fx_z63_probe_module_result(1u,(uint32_t)r,0u);return;}
    void *(*allocate)(uint32_t,uint32_t)=(void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u;
    Owner *s=allocate((uint32_t)sizeof(*s),0u);
    void *memory=allocate(layout.bytes+4096u,0u);
    if(!s || !memory){fx_z63_probe_module_result(2u,FX_BUDGET,0u);return;}
    memset(s,0,sizeof(*s));
    r=fx_z63_session_init(memory,layout.bytes+4096u,FX_Z63_PROBE_BYTES,65536u,&s->session,&s->buffers);
    fx_z63_probe_module_result(3u,(uint32_t)r,(uintptr_t)memory);
    if(r!=FX_OK)return;
    r=fx_z63_task_start(&s->task,run,s,s->buffers.kernel_stack,s->buffers.stack_bytes);
    fx_z63_probe_module_result(4u,(uint32_t)r,(uint32_t)s->task.id);
}
