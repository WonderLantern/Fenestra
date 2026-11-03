/* Z8 exec-context unit fixture: runs the production private gate with a
 * benign and a faulting operation, verifying check counting, injected fault
 * accounting and the faults gate on subsequent calls. */
#include "exec_context.h"

static _Alignas(4096) uint64_t root[512];
static FxZ8ExecContext context;

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
static int benign(FxZ8ExecContext *c) {
    (void)c;
    return 0;                                   /* FX_OK: no state touched */
}
static int faulting(FxZ8ExecContext *c) {
    fx_z8_private_fault(c,UINT64_C(0x86000011),UINT64_C(0x401a1234),UINT64_C(0x41000000),0u);
    return FX_INTEGRITY;
}
void fixture_main(void) {
    /* Benign call: one context check, snapshot restored, no faults. */
    FxResult r=fx_z8_exec_call(&context,(uintptr_t)root,benign);
    require(r==FX_OK,"benign gate call");
    require(context.diagnostics.context_checks==1u,"benign counts one check");
    require(!context.diagnostics.context_errors && !context.diagnostics.faults,"benign leaves clean diagnostics");
    require(context.user_entries==0u && context.user_exits==0u,"no EL0 involvement");
    /* Faulting call: injected ESR/ELR/FAR recorded once, error pinned. */
    r=fx_z8_exec_call(&context,(uintptr_t)root,faulting);
    require(r==FX_INTEGRITY,"faulting gate call reports integrity");
    require(context.diagnostics.faults==1u,"fault counted once");
    require(context.diagnostics.esr==UINT64_C(0x86000011) &&
            context.diagnostics.elr==UINT64_C(0x401a1234) &&
            context.diagnostics.far==UINT64_C(0x41000000),"fault registers recorded");
    require(context.diagnostics.error==FX_INTEGRITY,"fault pins error");
    /* Faults gate: further calls are rejected without running the gate. */
    r=fx_z8_exec_call(&context,(uintptr_t)root,benign);
    require(r==FX_INTEGRITY,"faults gate rejects reuse");
    require(context.diagnostics.context_checks==2u,"rejected call still counts");
    output("PASS Z8 exec_context check-count fault-injection faults-gate\n");
    finish(0u);
}
