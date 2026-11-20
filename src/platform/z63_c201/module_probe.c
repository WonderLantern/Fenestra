/* Explicit emulator bring-up: one pinned headless card module, no discovery. */
#include "module_session.h"
#include "task.h"
#ifdef FX_Z63_DISCOVERY
#include "discovery.h"
#endif
#include "fenestra_services.h"
#include "fenestra_memory.h"
#include "z63_probe_module.h"
#ifdef FX_Z63_SDK
#include "services.h"
#include "fenestra_platform_native.h"
#define CAPS 0x7f0000u
void fx_native_module_fault(FxPlatformOwner *owner,FxResult error) {
    if(!owner)return;
    if(error==FX_OK)error=FX_INTEGRITY;
    uint32_t zero=0u;
    (void)atomic_compare_exchange_strong(&owner->first_error,&zero,(uint32_t)error);
    atomic_store_explicit(&owner->phase,FX_OWNER_FAULT,memory_order_release);
}
#else
#define CAPS 0u
#endif
typedef struct {FxZ63ModuleSession *session;FxZ63SessionBuffers buffers;FxZ63TaskScope task;
#ifdef FX_Z63_SDK
FxZ63Services services;FxServiceOwner service;
#endif
} Owner;
__attribute__((noinline)) void fx_z63_probe_module_result(uint32_t stage,uint32_t result,uint64_t detail) {
    __asm__ volatile(""::"r"(stage),"r"(result),"r"(detail):"memory");
}
#ifdef FX_Z63_DISCOVERY
static void *index_allocate(void *context,uint32_t n) {(void)context;return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u)(n,0u);}
static void index_release(void *context,void *p) {(void)context;((void (*)(void *))(uintptr_t)0x40d32d40u)(p);}
#endif
static void run(void *argument) {
    Owner *s=argument;
#ifdef FX_Z63_DISCOVERY
    FxZ63Directory directory={0};FxModuleDirectory io;FxModuleInventory inventory={0};
    uint8_t target[32]={0x23,0x6b,0xbd,0x87,0x4e,0x09,0x42,0xfd,0x91,0xfb,0x54,0xa9,0x9a,0xf4,0x56,0xf4,0xeb,0xcc,0x0f,0xd9,0x66,0xaf,0x93,0x07,0x53,0xc4,0x3e,0xfd,0x18,0x39,0xfe,0x87};
    uint8_t scratch[4096];FxResult discovered=fx_z63_directory_init(&directory,&io);
    if(discovered==FX_OK)discovered=fx_modules_discover(&io,target,CAPS,scratch,sizeof(scratch),&inventory,(FxIndexMemory){0,index_allocate,index_release});
    fx_z63_probe_module_result(6u,(uint32_t)discovered,inventory.count);
    fx_z63_probe_module_result(7u,directory.native_error,directory.uncertain);
    for(uint32_t i=0;i<inventory.count;++i)fx_z63_probe_module_result(8u,inventory.entries[i].bytes,inventory.entries[i].image_bytes);
    fx_modules_destroy(&inventory);
    if(discovered!=FX_OK)return;
#endif
    FxResult r=fx_z63_session_load(s->session,"A:\\FENESTRA\\demo-z63.wlm","FENESTRA/demo-z63.wlm",FX_Z63_PROBE_BYTES,fx_z63_probe_digest,CAPS);
    fx_z63_probe_module_result(10u,(uint32_t)r,fx_z63_session_source_error(s->session));
    if(r!=FX_OK)return;
#ifdef FX_Z63_SDK
    uint32_t caps=(uint32_t)s->buffers.module->required_capabilities&CAPS;
    r=fx_z63_services_init(&s->services,1u,1u,caps);
    if(r==FX_OK)r=fx_platform_bind_module(&s->services.platform,s->buffers.module,s->buffers.image,s->buffers.image_bytes);
    if(r!=FX_OK){fx_z63_probe_module_result(40u,(uint32_t)r,0u);return;}
    fx_service_init(&s->service,1u,1u,caps,(FxServiceBackend){&s->services,fx_z63_service_call,fx_z63_service_stop});
    (void)fx_service_event(&s->service,FX_EVENT_INIT,0u);
    for(uint32_t i=0;i<128u;++i) {
        FxServiceModule *api=s->buffers.api;FxServiceExchange *exchange=s->buffers.exchange;
        memset(api,0,sizeof(*api));api->base=(FxModuleServices){FX_ABI,sizeof(*api),0,0};
        api->service_abi=FX_SERVICE_ABI;api->owner=1u;api->generation=1u;api->capabilities=caps;api->exchange=exchange;
        int result=-1;
        for(uint32_t attempts=0;attempts<100u;++attempts){r=fx_z63_session_invoke(s->session,100u,&result);if(r!=FX_INTERRUPTED)break;}
        if(r!=FX_OK || result){fx_z63_probe_module_result(40u,(uint32_t)r,(uint32_t)result);return;}
        FxServiceRequest request;FxServiceReply reply;
        fx_ram_copy(&request,&exchange->request,sizeof(request));
        r=fx_service_dispatch(&s->service,&request,&reply);
        fx_ram_copy(&exchange->reply,&reply,sizeof(reply));
        fx_z63_probe_module_result(50u,(uint32_t)r,request.operation);
        if(r==FX_OK && request.operation==FX_S_LIFECYCLE && request.arg[0]==FX_LIFE_FINISH)break;
        if(i==127u){fx_z63_probe_module_result(40u,FX_TIMEOUT,0u);return;}
    }
    r=fx_z63_session_close(s->session);fx_z63_probe_module_result(30u,(uint32_t)r,0u);
#ifdef FX_Z63_DRAWING
    extern void fx_z63_draw_probe(void);fx_z63_draw_probe();
#endif
    return;
#endif
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
