/* Isolated Cortex-A53 architecture fixture. Uses the real Zf page-table,
 * arena, runtime and assembly gates, with only camera I/O substituted. */
#include "owner_storage.h"
#include "fenestra_memory.h"

static FxZfOwnerStorage owner;
static _Alignas(4096) uint64_t root[512],level2[512];
static _Alignas(4096) uint8_t heap[1024u*1024u];
static uint32_t heap_used,events[4],event_count;
static int arena_ready;
static void output(const char *s) {
    volatile uint32_t *uart=(void *)(uintptr_t)0x09000000u;
    while(*s) *uart=(uint32_t)(uint8_t)*s++;
}
static void finish(int status) {
    uint64_t args[2]={0x20026u,(uint64_t)(unsigned)status};
    register uint64_t op __asm__("x0")=0x20u;
    register void *arg __asm__("x1")=args;
    __asm__ volatile("hlt #0xf000" : "+r"(op),"+r"(arg)::"memory");
    for(;;)__asm__ volatile("wfi");
}
static void require(int ok,const char *message) {
    if(!ok){output("FAIL ");output(message);output("\n");finish(1);}
}
static void hex(uint64_t value) {
    const char digits[]="0123456789abcdef";char text[18];
    for(unsigned i=0;i<16u;++i)text[i]=digits[(value>>((15u-i)*4u))&15u];
    text[16]='\n';text[17]=0;output(text);
}
static uint64_t clock_ms(void) {
    uint64_t count,freq;
    __asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0":"=r"(count),"=r"(freq));
    return count/freq*1000u+count%freq*1000u/freq;
}
void *fixture_allocate(uint32_t bytes) {
    uint32_t at=(heap_used+15u)&~15u;
    if(bytes>sizeof(heap)-at)return 0;
    heap_used=at+bytes;return heap+at;
}
#include "module_execution.h"

static void mmu(void) {
    /* Identity map RAM plus PL011. No camera addresses or original firmware. */
    for(uint32_t i=0;i<512u;++i)
        level2[i]=((uint64_t)0x40000000u+(uint64_t)i*0x200000u)|0x701u;
    root[1]=(uintptr_t)level2|3u;
    root[0]=UINT64_C(0x0060000000000705);
    uint64_t mair=0x04ffu,tcr=UINT64_C(0x200803519),control;
    __asm__ volatile("msr mair_el1,%0; msr tcr_el1,%1; msr ttbr0_el1,%2;"
        "msr ttbr1_el1,xzr; dsb sy; tlbi vmalle1; dsb sy; isb;"
        ::"r"(mair),"r"(tcr),"r"((uintptr_t)root):"memory");
    __asm__ volatile("mrs %0,sctlr_el1":"=r"(control));
    control|=1u;
    __asm__ volatile("msr sctlr_el1,%0; isb"::"r"(control):"memory");
}
FxResult fx_zf_services_prepare(FxZfServices *s,uint64_t now,uint32_t event,uint32_t value) {
    s->module.base=(FxModuleServices){FX_ABI,sizeof(s->module),0,0};
    s->module.service_abi=FX_SERVICE_ABI;s->module.exchange=&s->exchange;
    s->module.capabilities=(uint32_t)owner.module_runtime.stage.loader.module.required_capabilities;
    s->module.time_ms=now;s->module.event=event;s->module.event_value=value;return FX_OK;
}
FxResult fx_zf_services_dispatch_pending(FxZfServices *s) {
#if defined(FX_GATE_WLM)
    require(s->exchange.request.sequence==s->module.event,"WLM request copy");
#else
    require(s->exchange.request.sequence==1u,"EL0 request copy");
#endif
    if(event_count<4u)events[event_count++]=(uint32_t)s->exchange.request.arg[0];
    return FX_OK;
}
FxResult fx_zf_services_stop(FxZfServices *s,uint32_t panic) {(void)s;(void)panic;return FX_OK;}
FxResult fx_zf_services_init(FxZfServices *s,uint32_t id,uint32_t gen,uint32_t caps) {
    (void)s;(void)id;(void)gen;(void)caps;return FX_OK;
}
FxResult fx_zf_services_bind_module(FxZfServices *s,const char *id,const FxModule *m,void *p,uint32_t n) {
    (void)s;(void)id;(void)m;(void)p;(void)n;return FX_OK;
}
FxResult fx_zf_module_stage(FxZfModuleStage *s,const FxZfCardNative *n,const char *a,
    const char *b,uint32_t c,const uint8_t d[32],uint64_t e,uint8_t *f,uint32_t g,
    uint8_t *h,uint32_t i,uint8_t *j,uint32_t k,uint64_t l) {
    (void)s;(void)n;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;
    (void)g;(void)h;(void)i;(void)j;(void)k;(void)l;return FX_CAPABILITY;
}
void fixture_owner_stack(uintptr_t stack);
#if defined(FX_GATE_WLM)
extern const uint8_t fixture_package_begin[],fixture_package_end[];
static uint8_t package_buffer[16384];
static uint32_t card_position;
static int card_open(void *context,const char *path,uint64_t *size) {
    (void)context;(void)path;card_position=0u;
    *size=(uint64_t)(fixture_package_end-fixture_package_begin);return 0;
}
static int card_read(void *context,uint8_t *out,uint32_t bytes,uint32_t *actual) {
    (void)context;uint32_t size=(uint32_t)(fixture_package_end-fixture_package_begin);
    if(bytes>size-card_position)bytes=size-card_position;
    memcpy(out,fixture_package_begin+card_position,bytes);card_position+=bytes;*actual=bytes;return 0;
}
static int card_close(void *context) {(void)context;return 0;}
#endif
void fixture_main(void) {
    mmu();
    fixture_owner_stack((uintptr_t)owner.worker.stack+sizeof(owner.worker.stack));
}
void fixture_work(void) {
    FxLoader *loader=&owner.module_runtime.stage.loader;
#if defined(FX_GATE_WLM)
    const uint8_t target[32]={0x68,0x91,0xed,0xd2,0x2d,0x9a,0x40,0x04,
        0x2e,0x47,0x18,0x48,0x0d,0x29,0x08,0x26,0x82,0xbb,0xd1,0x2a,
        0xea,0x17,0x2c,0x51,0xf4,0xf1,0x64,0xb9,0x7f,0x74,0xfe,0x08};
    uint8_t digest[32];uint32_t bytes=(uint32_t)(fixture_package_end-fixture_package_begin);
    FxCardIO io={0,card_open,card_read,card_close};
    require(fx_zf_owner_module_reserve_image(&owner,65536u)==FX_OK,"reserve WLM image");
    fx_sha256(fixture_package_begin,bytes,digest);
    fx_loader_init(loader,FX_SDK_CAPS,131072u);
    require(fx_loader_prepare(loader,&io,"FENESTRA/gate-zf.wlm",bytes,digest,target,
        package_buffer,sizeof(package_buffer),owner.module_image_raw,65536u,clock_ms(),10000u)==FX_OK,
        "WLM card digest target segments relocation prepare");
    require(loader->module.entry_abi==FX_MODULE_ENTRY_SERVICES,"WLM service ABI");
#else
    fx_init(&loader->runtime,FX_SDK_CAPS,65536u);
    require(fx_begin(&loader->runtime,0u,8192u,0u,10000u)==FX_OK,"begin");
    require(fx_prepared(&loader->runtime,0u)==FX_OK,"prepared");
    owner.module_runtime.phase=FX_ZF_MODULE_BOUND;
    owner.module_runtime.services.module.generation=1u;
    loader->module=(FxModule){.abi=FX_ABI,.isa=FX_ISA_AARCH64,.bytes=8192u,
        .entry_offset=0u,.segment_count=1u,.prepared=1u,
        .stacks={FX_MODULE_STACK_DEFAULT,FX_MODULE_STACK_DEFAULT}};
    loader->module.segments[0]=(FxSegment){0u,0u,48u,48u,FX_READ|FX_EXECUTE,4096u};
    require(fx_zf_owner_module_reserve_image(&owner,8192u)==FX_OK,"reserve");
    loader->image=owner.module_image_raw;
    /* Copied module: request.sequence=1, arg0=event, return success. */
    const uint32_t code[]={0xf9401801u,0xb9403802u,0x52800023u,0xb9000023u,
        0xf9000822u,0x52800000u,0xd65f03c0u};
    memcpy(loader->image,code,sizeof(code));
#endif
    owner.module_runtime.phase=FX_ZF_MODULE_BOUND;
    owner.module_runtime.services.module.generation=1u;
    require(fx_zf_owner_module_prepare(&owner)==FX_OK,"arena prepare");
    arena_ready=1;
    require(owner.module_exec_context.diagnostics.context_errors==0u,"prepare context");
    FxResult init=fx_zf_module_runtime_activate(&owner.module_runtime,clock_ms());
    if(init!=FX_OK) {
        output("INIT result=");hex((uint64_t)init);
        output("runtime phase=");hex(loader->runtime.phase);
        output("faults=");hex(owner.module_exec_context.diagnostics.faults);
        output("context errors=");hex(owner.module_exec_context.diagnostics.context_errors);
    }
    require(init==FX_OK,"INIT");
    int result=0;
    require(fx_zf_module_runtime_invoke(&owner.module_runtime,clock_ms(),&result)==FX_OK,"TICK");
    require(result==0,"module return value");
    require(fx_zf_module_runtime_stop(&owner.module_runtime,clock_ms(),0u)==FX_OK,"STOP");
    require(event_count==3u && events[0]==FX_EVENT_INIT && events[1]==FX_EVENT_TICK &&
        events[2]==FX_EVENT_STOP,"event order");
    require(owner.module_exec_context.user_entries==3u && owner.module_exec_context.user_exits==3u,
        "SVC return count");
    require(!owner.module_exec_context.diagnostics.faults &&
        !owner.module_exec_context.diagnostics.context_errors,"context restored");
    require(arena_ready && !owner.module_arena.bound && !owner.module_arena.published,"revoked");
#if defined(FX_GATE_WLM)
    output("PASS WLM card digest target loader arena INIT TICK STOP EL0 returns=3 context-restored revoke\n");
#else
    output("PASS real Zf arena INIT TICK STOP EL0 returns=3 context-restored revoke\n");
#endif
    finish(0);
}
