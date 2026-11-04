#if !defined(FX_ZF_EXPERIMENTAL_EXEC_CONTEXT) || FX_ZF_EXPERIMENTAL_EXEC_CONTEXT != 1
#error "Zf private execution context is experimental"
#endif
#include "exec_context.h"

extern int fx_zf_private_call(uintptr_t,int (*)(FxZfExecContext *),FxZfExecContext *);

static void snapshot(uint64_t out[9]) {
    __asm__ volatile("mrs %0,daif; mrs %1,ttbr0_el1; mrs %2,ttbr1_el1; mrs %3,tcr_el1;"
        "mrs %4,mair_el1; mrs %5,vbar_el1; mrs %6,tpidrro_el0; mrs %7,spsel; mrs %8,CurrentEL"
        :"=r"(out[0]),"=r"(out[1]),"=r"(out[2]),"=r"(out[3]),"=r"(out[4]),"=r"(out[5]),
         "=r"(out[6]),"=r"(out[7]),"=r"(out[8])::"memory");
}

void fx_zf_private_fault(FxZfExecContext *context,uint64_t esr,uint64_t elr,uint64_t far) {
    if(!context->diagnostics.faults) {
        context->diagnostics.esr=esr;
        context->diagnostics.elr=elr;
        context->diagnostics.far=far;
    }
    if(context->diagnostics.faults!=UINT32_MAX)++context->diagnostics.faults;
    context->diagnostics.error=FX_INTEGRITY;
}

FxResult fx_zf_exec_call(FxZfExecContext *context,uintptr_t root,
                         int (*operation)(FxZfExecContext *)) {
    if(!context || !root || (root&4095u) || !operation)return FX_ARGUMENT;
    if(context->diagnostics.faults || context->diagnostics.context_errors)return FX_INTEGRITY;
    snapshot(context->diagnostics.before);
    if(context->diagnostics.before[8]!=4u)return FX_CAPABILITY;
    __asm__ volatile("mrs %0,sctlr_el1; mrs %1,cpacr_el1"
        :"=r"(context->before_control[0]),"=r"(context->before_control[1])::"memory");
    FxResult result=(FxResult)fx_zf_private_call(root,operation,context);
    __asm__ volatile("mrs %0,sctlr_el1; mrs %1,cpacr_el1"
        :"=r"(context->after_control[0]),"=r"(context->after_control[1])::"memory");
    snapshot(context->diagnostics.after);
    ++context->diagnostics.context_checks;
    for(uint32_t i=0;i<9u;++i)if(context->diagnostics.before[i]!=context->diagnostics.after[i]) {
        ++context->diagnostics.context_errors;
        context->diagnostics.reserved=i+1u;
        context->diagnostics.error=FX_INTEGRITY;
        return FX_INTEGRITY;
    }
    for(uint32_t i=0;i<2u;++i)if(context->before_control[i]!=context->after_control[i]) {
        ++context->diagnostics.context_errors;
        context->diagnostics.reserved=10u+i;
        context->diagnostics.error=FX_INTEGRITY;
        return FX_INTEGRITY;
    }
    return result;
}
