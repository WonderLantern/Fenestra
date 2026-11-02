/* Executes the production session, loader, card wrapper and architecture gates.
 * Only Nikon heap/file IO is replaced; high RAM and identity checks stay real. */
#include "module_session.h"
#include "fenestra_services.h"
#include "fenestra_memory.h"
static _Alignas(4096) uint64_t root[512],low[512];
static _Alignas(16) uint8_t scratch[32768];
static FxZfModuleSession *session;
static FxZfSessionBuffers buffers;
static uint8_t digest[32];
static uint32_t position,allocations,releases;
extern const uint8_t fixture_package_begin[],fixture_package_end[];
void fixture_owner_stack(uintptr_t);
static void output(const char *p) {
    while(*p)*(volatile uint32_t *)(uintptr_t)0x09000000u=(uint8_t)*p++;
}
static void finish(unsigned status) {
    uint64_t args[2]={0x20026u,status};
    register uint64_t op __asm__("x0")=0x20u;
    register void *arg __asm__("x1")=args;
    __asm__ volatile("hlt #0xf000":"+r"(op),"+r"(arg)::"memory");
    for(;;)__asm__ volatile("wfi");
}
static void require(int ok,const char *message) {
    if(!ok){output("FAIL ");output(message);output("\n");finish(1u);}
}
static void number(uint64_t n) {
    char p[18];const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16u;++i)p[i]=hex[(n>>((15u-i)*4u))&15u];
    p[16]='\n';p[17]=0;output(p);
}
void *fixture_allocate(uint32_t bytes) {
    require(bytes<=sizeof(scratch) && allocations==releases,"scratch lifetime");
    ++allocations;return scratch;
}
void fixture_release(void *p) {require(p==scratch,"scratch release");++releases;}
static int32_t open_file(const char *path,uint32_t flags,uint32_t mode) {
    require(!memcmp(path,"A:\\FENESTRA\\gate-zf.wlm",24u) && !flags && !mode,"card path");
    position=0;return 3;
}
static uint32_t read_file(int32_t fd,void *p,uint32_t bytes,uint32_t *done) {
    require(fd==3,"card handle");
    uint32_t total=(uint32_t)(fixture_package_end-fixture_package_begin);
    if(bytes>total-position)bytes=total-position;
    memcpy(p,fixture_package_begin+position,bytes);position+=bytes;*done=bytes;return 0;
}
static uint32_t close_file(int32_t fd){require(fd==3,"close handle");return 0;}
const FxZfCardNative *fx_zf_card_native_ops(void) {
    static const FxZfCardNative ops={open_file,read_file,close_file};return &ops;
}
void fixture_main(void) {
    for(uint32_t i=0;i<512u;++i)low[i]=(UINT64_C(0x40000000)+(uint64_t)i*0x200000u)|0x701u;
    root[1]=(uintptr_t)low|3u;
    root[0]=UINT64_C(0x0060000000000705);
    root[40]=UINT64_C(0xa00000000)|0x701u;
    uint64_t mair=0x04ffu,tcr=UINT64_C(0x200803519),control;
    __asm__ volatile("msr mair_el1,%0; msr tcr_el1,%1; msr ttbr0_el1,%2;"
        "msr ttbr1_el1,xzr; dsb sy; tlbi vmalle1; dsb sy; isb"
        ::"r"(mair),"r"(tcr),"r"((uintptr_t)root):"memory");
    __asm__ volatile("mrs %0,sctlr_el1":"=r"(control));control|=1u;
    __asm__ volatile("msr sctlr_el1,%0; isb"::"r"(control):"memory");
    uint32_t bytes=(uint32_t)(fixture_package_end-fixture_package_begin);FxExecLayout layout;
    require(fx_zf_session_layout(bytes,65536u,&layout)==FX_OK,"layout");
    fx_sha256(fixture_package_begin,bytes,digest);
    require(fx_zf_session_init((void *)(uintptr_t)UINT64_C(0xa00100000),layout.bytes+4096u,
        bytes,65536u,&session,&buffers)==FX_OK,"session init");
    fixture_owner_stack((uintptr_t)buffers.kernel_stack+buffers.stack_bytes);
}
void fixture_work(void) {
    uint32_t bytes=(uint32_t)(fixture_package_end-fixture_package_begin);
    FxResult r=fx_zf_session_load(session,"A:\\FENESTRA\\gate-zf.wlm","FENESTRA/gate-zf.wlm",bytes,digest,FX_SDK_CAPS);
    if(r!=FX_OK){output("load result ");number((uint64_t)r);}
    require(r==FX_OK,"production session load");
    require(allocations==1u && releases==1u,"scratch released before execute");
    const uint32_t events[]={FX_EVENT_INIT,FX_EVENT_TICK,FX_EVENT_STOP};
    for(uint32_t i=0u;i<3u;++i) {
        uint32_t event=events[i];
        FxServiceModule *api=buffers.api;FxServiceExchange *exchange=buffers.exchange;
        memset(api,0,sizeof(*api));memset(exchange,0,sizeof(*exchange));
        api->base=(FxModuleServices){FX_ABI,sizeof(*api),0,0};
        api->service_abi=FX_SERVICE_ABI;api->owner=1u;api->generation=1u;
        api->capabilities=(uint32_t)buffers.module->required_capabilities;
        api->exchange=exchange;api->event=event;
        int result=-99;r=fx_zf_session_invoke(session,100u,&result);
        if(r!=FX_OK){output("invoke result ");number((uint64_t)r);}
        require(r==FX_OK && result==0,"EL0 entry and return");
        require(exchange->request.sequence==event && exchange->request.arg[0]==event,"copied event request");
    }
    const FxZfExecDiagnostics *d=fx_zf_session_diagnostics(session);
    require(d && !d->faults && !d->context_errors && d->context_checks==4u,"original context restored");
    require(fx_zf_session_close(session)==FX_OK,"close admission");
    int result;require(fx_zf_session_invoke(session,100u,&result)==FX_STATE,"closed session rejected");
    output("PASS production Zf session WLM load scratch-release INIT TICK STOP EL0 returns=3 context-restored close\n");
    finish(0u);
}
