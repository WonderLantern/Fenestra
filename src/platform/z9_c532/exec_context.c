#include "exec_context.h"
_Static_assert(FX_INTEGRITY==10 && FX_INTERRUPTED==13,"assembly return ABI");
extern int fx_z9_private_call(uintptr_t,int (*)(FxZ9ExecContext *),FxZ9ExecContext *);
static void snapshot(uint64_t out[9]) {
    __asm__ volatile("mrs %0,daif; mrs %1,ttbr0_el1; mrs %2,ttbr1_el1; mrs %3,tcr_el1;"
        "mrs %4,mair_el1; mrs %5,vbar_el1; mrs %6,tpidrro_el0; mrs %7,spsel; mrs %8,CurrentEL"
        :"=r"(out[0]),"=r"(out[1]),"=r"(out[2]),"=r"(out[3]),"=r"(out[4]),"=r"(out[5]),
         "=r"(out[6]),"=r"(out[7]),"=r"(out[8])::"memory");
}
void fx_z9_private_fault(FxZ9ExecContext *s,uint64_t esr,uint64_t elr,uint64_t far,uint64_t argument) {
    (void)argument;
    if(!s->diagnostics.faults){s->diagnostics.esr=esr;s->diagnostics.elr=elr;s->diagnostics.far=far;}
    if(s->diagnostics.faults!=UINT32_MAX)++s->diagnostics.faults;
    s->diagnostics.error=FX_INTEGRITY;
}
FxResult fx_z9_exec_call(FxZ9ExecContext *s,uintptr_t root,int (*operation)(FxZ9ExecContext *)) {
    if(!s || !root || (root&4095u) || !operation)return FX_ARGUMENT;
    if(s->diagnostics.faults || s->diagnostics.context_errors)return FX_INTEGRITY;
    snapshot(s->diagnostics.before);
    if(s->diagnostics.before[8]!=4u)return FX_CAPABILITY;
    __asm__ volatile("mrs %0,sctlr_el1; mrs %1,cpacr_el1":"=r"(s->before_control[0]),"=r"(s->before_control[1])::"memory");
    FxResult result=(FxResult)fx_z9_private_call(root,operation,s);
    __asm__ volatile("mrs %0,sctlr_el1; mrs %1,cpacr_el1":"=r"(s->after_control[0]),"=r"(s->after_control[1])::"memory");
    snapshot(s->diagnostics.after);++s->diagnostics.context_checks;
    for(uint32_t i=0;i<9u;++i)if(s->diagnostics.before[i]!=s->diagnostics.after[i]) {
        ++s->diagnostics.context_errors;s->diagnostics.error=FX_INTEGRITY;return FX_INTEGRITY;
    }
    for(uint32_t i=0;i<2u;++i)if(s->before_control[i]!=s->after_control[i]) {
        ++s->diagnostics.context_errors;s->diagnostics.error=FX_INTEGRITY;return FX_INTEGRITY;
    }
    return result;
}
