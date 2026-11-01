#include "module_session.h"
#include "fenestra_exec.h"
#include "fenestra_services.h"
#include "fenestra_memory.h"
#include "fenestra_module_load_buffer.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#define PACKAGE FX_NATIVE_MODULE_PACKAGE_LIMIT
#define IMAGE FX_NATIVE_MODULE_IMAGE_LIMIT
#define EXCHANGE FX_SERVICE_EXCHANGE_BYTES
_Static_assert(sizeof(FxServiceExchange)<=FX_SERVICE_EXCHANGE_BYTES,"service wire fits copied exchange pages");
#define CODE_BEGIN UINT64_C(0x401a0000)
#define CODE_END UINT64_C(0x40200000)
enum {EMPTY,STAGED,READY,FAILED,CLOSED};
struct FxZ8ModuleSession {
    FxZ8ExecContext exec;FxZ8ModuleStage stage;FxExecArena arena;
    FxZ8AddressSpace private_space,user_space;FxLease lease;
    uintptr_t base,end,private_root,user_root;
    uint8_t *api,*exchange,*native_stack,*user_stack,*image;
    FxModuleLoadBuffer load_buffer;
    uint64_t (*private_tables)[512],(*user_tables)[512];volatile uint64_t *image_leaf;
    FxExecLayout layout;
    uint64_t deadline;uint32_t phase,operation,busy,closed,retained,reserved;
    int module_result;
};
_Static_assert(offsetof(FxZ8ModuleSession,exec)==0,"trusted architecture context prefix");
FxResult fx_z8_session_account(FxZ8ModuleSession *s,FxAccountRow *row) {
    if(!s || s->busy)return FX_STATE;
    return fx_module_load_buffer_account(&s->load_buffer,row);
}
static uintptr_t align(uintptr_t n){return (n+4095u)&~(uintptr_t)4095u;}
FxResult fx_z8_session_file_observer(FxZ8ModuleSession *s,FxFileObserver observer) {
    if(!s || s->busy || s->closed || s->phase!=EMPTY)return FX_STATE;
    s->stage.file_observer=observer;return FX_OK;
}
static int overlaps(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    return a>UINTPTR_MAX-an || b>UINTPTR_MAX-bn || (a<b+bn && b<a+an);
}
static uint64_t now(void) {
    uint64_t count,freq;
    __asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0":"=r"(count),"=r"(freq));
    if(!freq || freq>UINT32_MAX)return UINT64_MAX;
    return count/freq*1000u+count%freq*1000u/freq;
}
static FxResult fail(FxZ8ModuleSession *s,FxResult error) {
    s->closed=1u;s->phase=FAILED;s->retained=1u;
    if(!s->exec.diagnostics.error)s->exec.diagnostics.error=(uint32_t)error;
    (void)fx_panic(&s->stage.loader.runtime,error,s->exec.diagnostics.elr);
    return error;
}
static int identity(void *context,uintptr_t va,uint32_t flags) {
    FxZ8ModuleSession *s=context;uint64_t par;
    if(!((va>=s->base && va<=s->end-4096u) ||
         (!(flags&FX_Z8_MAP_WRITE) && va>=CODE_BEGIN && va<CODE_END)))return 0;
    if(flags&FX_Z8_MAP_WRITE)
        __asm__ volatile("at s1e1w,%1; isb; mrs %0,par_el1":"=r"(par):"r"(va):"memory");
    else __asm__ volatile("at s1e1r,%1; isb; mrs %0,par_el1":"=r"(par):"r"(va):"memory");
    return !(par&1u) && (par&FX_EXEC_PA_MASK)==va && (par>>56)==255u;
}
static volatile uint64_t *image_leaf(FxZ8ModuleSession *s) {
    uint64_t *table=s->private_tables[0];uintptr_t va=(uintptr_t)s->image;
    for(uint32_t shift=30u;shift>12u;shift-=9u) {
        uint64_t pte=table[(va>>shift)&511u];uintptr_t p=(uintptr_t)(pte&FX_EXEC_PA_MASK);
        if((pte&3u)!=3u || p<(uintptr_t)s->private_tables ||
           p>=(uintptr_t)s->private_tables+(uintptr_t)s->private_space.used*4096u)return 0;
        table=(uint64_t *)p;
    }
    volatile uint64_t *first=&table[(va>>12)&511u];
    /* The shared backend walks and validates every distinct L3 table. */
    return first;
}
FxResult fx_z8_session_layout(uint32_t package,uint32_t image,FxExecLayout *layout) {
    return fx_z8_session_layout_stacks(package,image,
        (FxModuleStacks){FX_MODULE_STACK_DEFAULT,FX_MODULE_STACK_DEFAULT},layout);
}
FxResult fx_z8_session_layout_stacks(uint32_t package,uint32_t image,FxModuleStacks stacks,FxExecLayout *layout) {
    if(package>PACKAGE || image>IMAGE)return FX_BOUNDS;
    return fx_exec_layout_stacks((uint32_t)sizeof(FxZ8ModuleSession),package,image,EXCHANGE,0u,stacks,layout);
}
FxResult fx_z8_session_init(void *allocation,uint32_t bytes,uint32_t package,uint32_t image,
                             FxZ8ModuleSession **out,FxZ8SessionBuffers *buffers) {
    return fx_z8_session_init_stacks(allocation,bytes,package,image,
        (FxModuleStacks){FX_MODULE_STACK_DEFAULT,FX_MODULE_STACK_DEFAULT},out,buffers);
}
FxResult fx_z8_session_init_stacks(void *allocation,uint32_t bytes,uint32_t package,uint32_t image,
                             FxModuleStacks stacks,FxZ8ModuleSession **out,FxZ8SessionBuffers *buffers) {
    FxExecLayout layout;FxResult planned=fx_z8_session_layout_stacks(package,image,stacks,&layout);
    if(planned!=FX_OK)return planned;
    if(!allocation || !out || *out || !buffers || bytes<layout.bytes+4096u || bytes>layout.bytes+8192u ||
       (uintptr_t)allocation<UINT64_C(0xa00000000) ||
       (uintptr_t)allocation>UINT64_C(0xa40000000)-bytes ||
       overlaps((uintptr_t)allocation,bytes,(uintptr_t)out,sizeof(*out)) ||
       overlaps((uintptr_t)allocation,bytes,(uintptr_t)buffers,sizeof(*buffers)) ||
       overlaps((uintptr_t)out,sizeof(*out),(uintptr_t)buffers,sizeof(*buffers)))return FX_ARGUMENT;
    uintptr_t base=align((uintptr_t)allocation),end=base+layout.bytes;
    if(end>(uintptr_t)allocation+bytes)return FX_BOUNDS;
    FxZ8ModuleSession *s=(FxZ8ModuleSession *)base;
    if(s->exec.diagnostics.magic==0x465a3953u && s->exec.diagnostics.version==1u)return FX_BUSY;
    memset(s,0,sizeof(*s));s->base=base;s->end=end;s->layout=layout;
    s->private_tables=(uint64_t (*)[512])(base+layout.private_tables);
    s->user_tables=(uint64_t (*)[512])(base+layout.user_tables);
    s->native_stack=(uint8_t *)(base+layout.native_stack);s->api=(uint8_t *)(base+layout.api);
    s->exchange=(uint8_t *)(base+layout.exchange);s->user_stack=(uint8_t *)(base+layout.user_stack);
    s->image=(uint8_t *)(base+layout.image);
    memset(s->api,0,4096u);memset(s->exchange,0,EXCHANGE);
    s->exec.diagnostics.magic=0x465a3953u;s->exec.diagnostics.version=1u;
    *buffers=(FxZ8SessionBuffers){s->api,s->exchange,s->native_stack,EXCHANGE,layout.native_stack_bytes,&s->stage.loader.module,s->image,layout.image_bytes};
    *out=s;return FX_OK;
}
static FxResult user_view(FxZ8ModuleSession *s) {
    FxResult r=fx_z8_space_init(&s->user_space,s->user_tables,s->layout.tables,s,identity);
    if(r!=FX_OK)return r;
    uintptr_t trampoline=(uintptr_t)fx_z8_user_return;
    if((trampoline&4095u) || trampoline<CODE_BEGIN || trampoline>CODE_END-4096u)return FX_CAPABILITY;
    for(uintptr_t va=s->base;va<s->end;va+=4096u) {
        uint32_t flags=FX_Z8_MAP_READ|FX_Z8_MAP_WRITE;
        if(va==(uintptr_t)s->user_stack-4096u || va==(uintptr_t)s->user_stack+s->layout.user_stack_bytes)continue;
        if(va==(uintptr_t)s->api)flags=FX_Z8_MAP_READ|FX_Z8_MAP_USER;
        else if((va>=(uintptr_t)s->exchange && va<(uintptr_t)s->exchange+EXCHANGE) ||
                (va>=(uintptr_t)s->user_stack && va<(uintptr_t)s->user_stack+s->layout.user_stack_bytes))flags|=FX_Z8_MAP_USER;
        else if(va>=(uintptr_t)s->image && va<(uintptr_t)s->image+s->layout.image_bytes) {
            uint64_t pte=s->arena.expected[(va-(uintptr_t)s->image)/4096u];if(!pte)continue;
            flags=FX_Z8_MAP_READ|FX_Z8_MAP_USER;
            if(!(pte&(UINT64_C(1)<<7)))flags|=FX_Z8_MAP_WRITE;
            if(!(pte&FX_EXEC_PXN))flags|=FX_Z8_MAP_EXECUTE;
        }
        r=fx_z8_space_map(&s->user_space,va,4096u,flags);if(r!=FX_OK)return r;
    }
    for(uintptr_t va=CODE_BEGIN;va<CODE_END;va+=4096u) {
        uint32_t flags=FX_Z8_MAP_READ|FX_Z8_MAP_EXECUTE;
        if(va==trampoline)flags|=FX_Z8_MAP_USER;
        r=fx_z8_space_map(&s->user_space,va,4096u,flags);if(r!=FX_OK)return r;
    }
    return fx_z8_space_seal(&s->user_space,&s->user_root);
}
static int operation(FxZ8ExecContext *context) {
    FxZ8ModuleSession *s=(FxZ8ModuleSession *)context;FxResult r;
    if(s->operation==0u) {
        r=fx_exec_bind(&s->arena,(uintptr_t)s->image,(uintptr_t)s->image,s->layout.image_bytes,s->image_leaf,
            (FxExecStorage){(uint64_t *)(s->base+s->layout.expected),(uint64_t *)(s->base+s->layout.staging),s->layout.image_bytes/4096u});
        if(r==FX_OK)r=fx_loader_activate_mapped(&s->stage.loader,&s->arena,now());
        if(r==FX_OK)r=user_view(s);
        if(r==FX_OK)r=fx_execution_idle(&s->stage.loader.runtime,now());
        return (int)r;
    }
    uint64_t clock=now();if(clock==UINT64_MAX || clock>=s->deadline)return FX_TIMEOUT;
    r=fx_exec_live(&s->arena);if(r!=FX_OK)return (int)r;
    r=fx_heartbeat(&s->stage.loader.runtime,clock);if(r!=FX_OK)return (int)r;
    s->module_result=fx_z8_user_call((uintptr_t)s->image+s->stage.loader.module.entry_offset,
        s->api,s->user_root,(uintptr_t)s->user_stack+s->layout.user_stack_bytes,&s->exec);
    if(s->exec.continuation[34])return FX_INTERRUPTED;
    clock=now();
    r=fx_release(&s->stage.loader.runtime,s->lease,clock);
    if(r==FX_OK)r=fx_execution_idle(&s->stage.loader.runtime,clock);
    return (int)r;
}
static int worker_stack(const FxZ8ModuleSession *s) {
    uintptr_t sp;__asm__ volatile("mov %0,sp":"=r"(sp));
    /* The private view contains only this owned native stack. Check while
     * the original tables are active, before entering the architectural gate.
     * Reserve room for nested plan/walk/exception frames, not just the SP. */
    return !(sp&15u) && sp>=(uintptr_t)s->native_stack+16384u &&
           sp<=(uintptr_t)s->native_stack+s->layout.native_stack_bytes;
}
static void *load_allocate(void *context,uint32_t bytes) {
    /* The original Heap pool (index 1) holds temporary, non-executable file
     * bytes. Retained sessions/images keep the separate General pool. */
    (void)context;return FN(UINT64_C(0x40d2c408),void *(*)(uint32_t,uint32_t))(bytes,1u);
}
static void load_release(void *context,void *data) {
    (void)context;FN(UINT64_C(0x40d2c448),void (*)(void *))(data);
}
FxResult fx_z8_session_load(FxZ8ModuleSession *s,const char *native_path,const char *logical_path,
                           uint32_t bytes,const uint8_t digest[32],uint64_t caps) {
    if(!s || s->closed || s->busy || s->phase!=EMPTY)return FX_STATE;
    if(!worker_stack(s))return fail(s,FX_CAPABILITY);
    s->busy=1u;uint64_t clock=now();
    FxResult r=clock==UINT64_MAX?FX_CAPABILITY:fx_module_load_buffer_open(&s->load_buffer,
        (FxIndexMemory){0,load_allocate,load_release},s->layout.package_bytes);
    if(r==FX_OK)r=fx_z8_module_stage(&s->stage,native_path,logical_path,bytes,digest,caps,
        s->load_buffer.data,s->load_buffer.bytes,s->load_buffer.data,s->layout.package_bytes,s->image,s->layout.image_bytes,clock);
    FxResult cleanup=fx_module_load_buffer_finish(&s->load_buffer,&s->stage.file,&s->stage.loader.card,r);
    if(r==FX_OK)r=cleanup;
    if(r==FX_OK)r=fx_loader_source_released(&s->stage.loader);
    if(r==FX_OK && (s->stage.loader.module.stacks.native_bytes!=s->layout.native_stack_bytes ||
                   s->stage.loader.module.stacks.user_bytes!=s->layout.user_stack_bytes))r=FX_INTEGRITY;
    if(r==FX_OK)r=fx_z8_space_init(&s->private_space,s->private_tables,s->layout.tables,s,identity);
    if(r==FX_OK)r=fx_z8_space_map(&s->private_space,s->base,s->layout.bytes,FX_Z8_MAP_READ|FX_Z8_MAP_WRITE);
    if(r==FX_OK)r=fx_z8_space_map(&s->private_space,CODE_BEGIN,(uint32_t)(CODE_END-CODE_BEGIN),FX_Z8_MAP_READ|FX_Z8_MAP_EXECUTE);
    if(r==FX_OK) {s->image_leaf=image_leaf(s);if(!s->image_leaf)r=FX_CAPABILITY;}
    if(r==FX_OK)r=fx_z8_space_seal(&s->private_space,&s->private_root);
    s->exec.simd_enabled=(s->stage.loader.module.required_capabilities&FX_SDK_SIMD)?1u:0u;
    if(r==FX_OK){s->phase=STAGED;s->operation=0u;r=fx_z8_exec_call(&s->exec,s->private_root,operation);}
    if(r==FX_OK && !s->exec.diagnostics.faults)s->phase=READY;else r=fail(s,r==FX_OK?FX_INTEGRITY:r);
    s->busy=0u;return r;
}
FxResult fx_z8_session_invoke(FxZ8ModuleSession *s,uint32_t budget,int *result) {
    if(!s || !result || !budget || budget>100u)return FX_ARGUMENT;
    *result=0;if(s->closed || s->busy || s->phase!=READY)return FX_STATE;
    if(!worker_stack(s))return fail(s,FX_CAPABILITY);
    const FxModuleServices *api=(const FxModuleServices *)s->api;
    if(api->abi!=FX_ABI || api->bytes<sizeof(*api) || api->bytes>4096u || api->context || api->log)return fail(s,FX_ARGUMENT);
    uint64_t clock=now();if(clock==UINT64_MAX || clock>UINT64_MAX-budget)return fail(s,FX_CAPABILITY);
    s->busy=1u;FxResult r=FX_OK;
    if(!s->exec.continuation[34]) {
        r=fx_execution_resume(&s->stage.loader.runtime,clock);
        if(r==FX_OK)r=fx_acquire(&s->stage.loader.runtime,s->stage.loader.runtime.generation,clock,&s->lease);
        s->deadline=clock+budget;
    }
    s->operation=1u;
    if(r==FX_OK)r=fx_z8_exec_call(&s->exec,s->private_root,operation);
    if(r==FX_OK && s->exec.diagnostics.faults)r=FX_INTEGRITY;
    if(r==FX_OK)*result=s->module_result;
    else if(r!=FX_INTERRUPTED)r=fail(s,r);
    s->busy=0u;return r;
}
int fx_z8_session_pending(const FxZ8ModuleSession *s){return s && s->exec.continuation[34]!=0;}
FxResult fx_z8_session_close(FxZ8ModuleSession *s) {
    if(!s)return FX_ARGUMENT;
    if(s->busy)return FX_BUSY;
    s->closed=1u;s->retained=1u;if(s->phase!=FAILED)s->phase=CLOSED;
    return FX_OK; /* Admission closed; native services/DMA still require their own stop ACK. */
}
const uint32_t fx_native_load_buffer_metadata[]={0x46584c42u,1u,
    offsetof(FxZ8ModuleSession,load_buffer),offsetof(FxModuleLoadBuffer,data),offsetof(FxModuleLoadBuffer,bytes),
    offsetof(FxModuleLoadBuffer,peak),offsetof(FxModuleLoadBuffer,released_bytes),offsetof(FxModuleLoadBuffer,releases),
    offsetof(FxModuleLoadBuffer,retained),offsetof(FxZ8ModuleSession,layout)+offsetof(FxExecLayout,bytes),
    offsetof(FxZ8ModuleSession,layout)+offsetof(FxExecLayout,package_bytes),sizeof(FxModuleLoadBuffer),
    offsetof(FxZ8ModuleSession,stage)+offsetof(FxZ8ModuleStage,loader)+offsetof(FxLoader,runtime)+offsetof(FxRuntime,reserved_bytes),
    offsetof(FxZ8ModuleSession,stage)+offsetof(FxZ8ModuleStage,loader)+offsetof(FxLoader,source_reserved),
    offsetof(FxZ8ModuleSession,stage)+offsetof(FxZ8ModuleStage,loader)+offsetof(FxLoader,card)+offsetof(FxCardReceipt,verified)};
const FxZ8ExecDiagnostics *fx_z8_session_diagnostics(const FxZ8ModuleSession *s){return s?&s->exec.diagnostics:0;}
