#include "module_worker.h"
#include "fenestra_memory.h"
#include "fenestra_platform.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_resources.h"
#include "services.h"
#include "fenestra_worker_wait.h"
#include "fenestra_worker_pace.h"
#include "fenestra_native_scene.h"
#include <stdatomic.h>
#define MAGIC UINT32_C(0x465a3957)
struct FxZ9ModuleWorker {
    uint32_t magic;_Atomic uint32_t phase,error,shutdown,action;
    FxZ9TaskScope task;FxZ9ModuleSession *session;FxZ9SessionBuffers buffers;
    FxDiscoveredModule entry;FxServiceOwner service;
    char native_path[64],logical_path[48];uint64_t next_due,stop_deadline;
    int32_t module_result;
    uint32_t return_valid;
    FxPanicReport panic_report;
    FxWorkerWait wait;uint64_t tick_due;FxWorkerPace pace;
};
const uint32_t fx_native_load_owner_metadata[]={0x46584c4fu,1u,offsetof(FxZ9ModuleWorker,session)};
static uint64_t clock_ms(void) {
    uint64_t n,hz;__asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0":"=r"(n),"=r"(hz));
    return hz && hz<=UINT32_MAX?n/hz*1000u+n%hz*1000u/hz:UINT64_MAX;
}
static int native_ram(const void *p,uint32_t n) {
    uintptr_t a=(uintptr_t)p;
    return p && !(a&7u) && a>=UINT64_C(0xa00000000) && a<=UINT64_C(0xa40000000)-n;
}
static int overlap(const void *a,uint32_t an,const void *b,uint32_t bn) {
    return (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}
static int callback(uintptr_t a){return !(a&3u) && a>=UINT64_C(0x401a0000) && a<UINT64_C(0x40200000);}
static void panic(FxZ9ModuleWorker *w,FxResult result) {
    if(atomic_load_explicit(&w->phase,memory_order_acquire)==FX_Z9_WORKER_PANIC)return;
    uint32_t zero=0u;atomic_compare_exchange_strong(&w->error,&zero,(uint32_t)result);
    atomic_store_explicit(&w->shutdown,1u,memory_order_release);
    /* The architecture gate has already restored original tables/context.
     * Close native hardware admission before recording the public phase. */
    fx_service_fault(&w->service);(void)fx_z9_session_close(w->session);
    w->panic_report=(FxPanicReport){.module=w->entry.file,.error=(FxResult)atomic_load(&w->error),
        .module_result=w->module_result,.return_valid=w->return_valid};
    const FxZ9ExecDiagnostics *diagnostics=fx_z9_session_diagnostics(w->session);
    if(diagnostics) {
        w->panic_report.cpu_faults=diagnostics->faults;
        w->panic_report.esr=diagnostics->esr;w->panic_report.elr=diagnostics->elr;
        w->panic_report.far=diagnostics->far;
    }
    fx_panic_service_request(&w->panic_report,&w->service);
    atomic_store_explicit(&w->phase,FX_Z9_WORKER_PANIC,memory_order_release);
}
static void park(FxZ9ModuleWorker *w,uint64_t ticket,uint32_t ticks) {
    uint32_t expired=0u;FxResult r=fx_worker_wait_step(&w->wait,ticket,ticks,&expired);
    if(r!=FX_OK)(void)fx_z9_task_delay_ticks(ticks==UINT32_MAX?100u:ticks>1000u?1000u:ticks);
}
static void run(void *context) {
    FxZ9ModuleWorker *w=context;
    FxPlatformOwner *platform=&((FxZ9Services *)w->service.backend.context)->platform;
    if(platform->accounting) {
        FxZ9Services *services=w->service.backend.context;
        FxResult account=fx_z9_session_account(w->session,&services->accounting.source);
        if(account!=FX_OK){panic(w,account);return;}
    }
    (void)fx_worker_wait_profile(&w->wait,platform->platform);
    FxResult r=fx_worker_wait_bind(platform,&w->wait);
    if(r!=FX_OK){panic(w,r);return;}
    r=fx_z9_session_file_observer(w->session,fx_capacity_file_observer(platform->capacity));
    if(r!=FX_OK){panic(w,r);return;}
    r=fx_z9_session_load(w->session,w->native_path,w->logical_path,w->entry.bytes,
                                w->entry.digest,w->service.capabilities);
    if(r==FX_OK && w->buffers.module->entry_abi!=FX_MODULE_ENTRY_SERVICES)r=FX_CAPABILITY;
    if(r==FX_OK)r=fx_platform_bind_module(&((FxZ9Services *)w->service.backend.context)->platform,
        w->buffers.module,w->buffers.image,w->buffers.image_bytes);
    if(r!=FX_OK)panic(w,r);
    else if(fx_service_event(&w->service,FX_EVENT_INIT,0u)!=FX_OK)panic(w,FX_BUSY);
    else {
        w->next_due=clock_ms();w->tick_due=w->next_due;
        atomic_store_explicit(&w->phase,FX_Z9_WORKER_ACTIVE,memory_order_release);
    }
    for(;;) {
        uint64_t loop_ticket=fx_worker_wait_ticket(&w->wait);
        fx_service_fault_poll(&w->service);
        FxPlatformOwner *owner=&((FxZ9Services *)w->service.backend.context)->platform;
        fx_native_scene_poll(owner);
        if(owner->phase==FX_OWNER_FAULT && atomic_load(&w->phase)!=FX_Z9_WORKER_PANIC)
            panic(w,owner->first_error?(FxResult)owner->first_error:FX_INTEGRITY);
        if(atomic_load_explicit(&w->phase,memory_order_acquire)==FX_Z9_WORKER_PANIC) {
            w->wait.armed=0u;
            if(w->service.stop_state==FX_SERVICE_STOP_FAULT_PENDING)(void)fx_z9_task_delay_ticks(100u);
            else park(w,loop_ticket,fx_native_scene_needs_poll(owner)?1u:UINT32_MAX);
            continue;
        }
        if(atomic_load_explicit(&w->shutdown,memory_order_acquire)) {
            atomic_store_explicit(&w->phase,FX_Z9_WORKER_STOPPING,memory_order_release);
            uint64_t now=clock_ms();
            if(now==UINT64_MAX || now>UINT64_MAX-2000u){panic(w,FX_CAPABILITY);continue;}
            if(!w->stop_deadline)w->stop_deadline=now+2000u;
            if(now>=w->stop_deadline){panic(w,FX_TIMEOUT);continue;}
            r=fx_service_stop(&w->service);
            if(r==FX_OK) {
                (void)fx_z9_session_close(w->session);
                atomic_store_explicit(&w->phase,FX_Z9_WORKER_QUIESCED,memory_order_release);
                for(;;)park(w,fx_worker_wait_ticket(&w->wait),UINT32_MAX);
            }
            if(r!=FX_BUSY)panic(w,r);
            else if(fx_z9_task_delay_ticks(1u)!=FX_OK)panic(w,FX_IO);
            continue;
        }
        uint64_t now=clock_ms();
        if(atomic_load_explicit(&w->phase,memory_order_acquire)==FX_Z9_WORKER_FINISHED) {
            /* FINISH queued its terminal UI change; the retained dispatcher
             * retries BUSY itself. Idle workers must not resubmit forever. */
            w->wait.armed=0u;park(w,loop_ticket,fx_native_scene_needs_poll(owner)?1u:UINT32_MAX);continue;
        }
        if(w->wait.armed) {
            uint32_t tick=atomic_load_explicit(&owner->tick_subscriptions,memory_order_acquire);
            if(tick && now>=w->tick_due){fx_platform_pulse(owner,now);w->tick_due=now+w->entry.period_ms;}
            (void)fx_worker_wait_continue(&w->wait,tick || fx_native_scene_needs_poll(owner)?1u:0u,w->buffers.exchange?&((FxServiceExchange *)w->buffers.exchange)->reply:0,&w->service);
            continue; /* Recheck shutdown/fault and read a fresh clock before EL0. */
        }
        if(now==UINT64_MAX || now>UINT64_MAX-w->entry.period_ms){panic(w,FX_CAPABILITY);continue;}
        uint32_t pacing=FX_WORKER_POLL;
        int pending=fx_z9_session_pending(w->session);
        if(pending || w->wait.resumed || now>=w->next_due) {
            FxServiceModule *api=w->buffers.api;
            FxServiceExchange *exchange=w->buffers.exchange;
            if(!pending) {
                w->wait.resumed=0u;
                uint32_t action=atomic_load_explicit(&w->action,memory_order_acquire);
                if(action && fx_service_event(&w->service,FX_EVENT_ACTION,action)==FX_OK)
                    (void)atomic_compare_exchange_strong(&w->action,&action,0u);
                memset(api,0,sizeof(*api));api->base.abi=FX_ABI;api->base.bytes=sizeof(*api);
                if(now>=w->tick_due){fx_platform_pulse(owner,now);w->tick_due=now+w->entry.period_ms;}
                api->service_abi=FX_SERVICE_ABI;api->owner=w->service.owner;
                api->generation=w->service.generation;api->capabilities=w->service.capabilities;
                api->time_ms=now;api->exchange=exchange;
            }
            w->module_result=INT32_MIN;
            w->return_valid=0u;
            int result=0;r=fx_z9_session_invoke(w->session,w->entry.budget_ms,&result);
            if(r==FX_OK) {
                w->module_result=result;
                w->return_valid=1u;
                if(result){panic(w,FX_INTEGRITY);continue;}
                /* Neither request pointers nor a native callback can be
                 * supplied by EL0. Copy only after its complete return. */
                FxServiceRequest request;fx_ram_copy(&request,&exchange->request,sizeof(request));FxServiceReply reply;
                w->service.time_ms=now;
                uint64_t previous=w->service.last_sequence;
                (void)fx_service_dispatch(&w->service,&request,&reply);
                pacing=fx_worker_pace_reply(&w->pace,previous,request.sequence,&reply);
                fx_ram_copy(&exchange->reply,&reply,sizeof(reply));
                if(reply.result==FX_OK && request.operation==FX_S_LIFECYCLE && request.arg[0]==FX_LIFE_FINISH)
                    atomic_store_explicit(&w->phase,FX_Z9_WORKER_FINISHED,memory_order_release);
                if(w->service.fault){panic(w,FX_INTEGRITY);continue;}
                w->next_due=pacing==FX_WORKER_POLL?now+w->entry.period_ms:now;
            } else if(r!=FX_INTERRUPTED){panic(w,r);continue;}
        }
        if(w->wait.armed || atomic_load(&w->phase)==FX_Z9_WORKER_FINISHED)continue;
        if(pacing==FX_WORKER_CONTINUE)continue;
        /* Preserve the legacy native-delay path; do not add flag create/clear/
         * wait calls to each tick of a periodic module such as an animation. */
        if(fx_z9_task_delay_ticks(1u)!=FX_OK)panic(w,FX_IO);
    }
}
uint32_t fx_z9_worker_bytes(void){return (uint32_t)sizeof(FxZ9ModuleWorker);}
FxResult fx_z9_worker_start(void *allocation,uint32_t bytes,void *session_allocation,uint32_t session_bytes,
                          const FxDiscoveredModule *entry,uint32_t owner,uint32_t generation,uint32_t capabilities,
                          FxServiceBackend backend,FxZ9ModuleWorker **out) {
    if(!out || *out || !entry || !owner || owner>65535u || !generation || !backend.call || !backend.stop ||
       !callback((uintptr_t)backend.call) || !callback((uintptr_t)backend.stop) ||
       !native_ram(allocation,bytes) || bytes<sizeof(FxZ9ModuleWorker) ||
       !native_ram(session_allocation,session_bytes) ||
       overlap(allocation,bytes,session_allocation,session_bytes) ||
       overlap(allocation,bytes,entry,sizeof(*entry)) || overlap(session_allocation,session_bytes,entry,sizeof(*entry)) ||
       overlap(allocation,bytes,out,sizeof(*out)) || overlap(session_allocation,session_bytes,out,sizeof(*out)) ||
       !entry->enabled || !fx_module_stacks_valid(entry->stacks) || entry->bytes<=128u || entry->bytes>FX_NATIVE_MODULE_PACKAGE_LIMIT || !entry->image_bytes || entry->image_bytes>FX_NATIVE_MODULE_IMAGE_LIMIT || entry->period_ms<10u || entry->period_ms>10000u ||
       !entry->budget_ms || entry->budget_ms>100u || (entry->required&~capabilities) ||
       !fx_service_name(entry->file,sizeof(entry->file),1))return FX_ARGUMENT;
    uint32_t n=0;while(n<sizeof(entry->file) && entry->file[n])++n;
    if(n<5u || n>=sizeof(entry->file) || memcmp(entry->file+n-4u,".wlm",4u))return FX_ARGUMENT;
    FxZ9ModuleWorker *w=allocation;
    if(w->magic==MAGIC)return FX_BUSY;
    memset(w,0,sizeof(*w));w->magic=MAGIC;w->entry=*entry;w->module_result=INT32_MIN;
    memcpy(w->native_path,"A:\\FENESTRA\\",12u);memcpy(w->native_path+12u,entry->file,n+1u);
    memcpy(w->logical_path,"FENESTRA/",9u);memcpy(w->logical_path+9u,entry->file,n+1u);
    (void)fx_worker_wait_init(&w->wait,0,entry->period_ms);
    fx_service_init(&w->service,owner,generation,capabilities,backend);
    w->service.wake_context=&w->wait;w->service.wake_callback=fx_worker_wait_signal;
    FxResult r=fx_z9_session_init_stacks(session_allocation,session_bytes,entry->bytes,entry->image_bytes,entry->stacks,&w->session,&w->buffers);
    if(r!=FX_OK){panic(w,r);return r;}
    atomic_store_explicit(&w->phase,FX_Z9_WORKER_LOADING,memory_order_release);*out=w;
    r=fx_z9_task_start(&w->task,run,w,w->buffers.kernel_stack,w->buffers.stack_bytes);
    if(r!=FX_OK)panic(w,r);
    return r;
}
FxResult fx_z9_worker_action(FxZ9ModuleWorker *w,uint32_t action) {
    if(!w || w->magic!=MAGIC || !action)return FX_ARGUMENT;
    if(atomic_load_explicit(&w->phase,memory_order_acquire)!=FX_Z9_WORKER_ACTIVE ||
       atomic_load_explicit(&w->shutdown,memory_order_acquire))return FX_STATE;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong_explicit(&w->action,&zero,action,memory_order_release,memory_order_relaxed))return FX_BUSY;
    fx_worker_wait_signal(&w->wait);return FX_OK;
}
void fx_z9_worker_shutdown(FxZ9ModuleWorker *w){if(w && w->magic==MAGIC){if(!atomic_exchange_explicit(&w->shutdown,1u,memory_order_acq_rel))fx_worker_wait_signal(&w->wait);}}
uint32_t fx_z9_worker_phase(const FxZ9ModuleWorker *w){return w && w->magic==MAGIC?atomic_load_explicit(&w->phase,memory_order_acquire):FX_Z9_WORKER_EMPTY;}
uint32_t fx_z9_worker_error(const FxZ9ModuleWorker *w){return w && w->magic==MAGIC?atomic_load_explicit(&w->error,memory_order_acquire):FX_ARGUMENT;}
const char *fx_z9_worker_identity(const FxZ9ModuleWorker *w){return w && w->magic==MAGIC?w->entry.id:0;}
FxResult fx_z9_worker_panic_report(const FxZ9ModuleWorker *w,FxPanicReport *report) {
    if(!report || !w || w->magic!=MAGIC)return FX_ARGUMENT;
    if(atomic_load_explicit(&w->phase,memory_order_acquire)!=FX_Z9_WORKER_PANIC)return FX_BUSY;
    *report=w->panic_report;return FX_OK;
}
