#include "fenestra_platform_native.h"
#include "fenestra_resources.h"
#include "fenestra_machine.h"
#include "fenestra_native_context.h"
#include "native_translation_impl.h"
uint64_t fx_native_translation(uint64_t address,uint64_t access,uint64_t a2,uint64_t a3,
                               uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7) {
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    return fx_translation_probe(address,access);
}
uint64_t fx_native_context(uint64_t field,uint64_t a1,uint64_t a2,uint64_t a3,
                           uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint64_t value;
#define CONTEXT(id,reg) case id:__asm__ volatile("mrs %0," #reg:"=r"(value));return value
    switch(field) {
        CONTEXT(FX_CONTEXT_TPIDR_EL1,tpidr_el1);
        CONTEXT(FX_CONTEXT_MPIDR_EL1,mpidr_el1);
        CONTEXT(FX_CONTEXT_CURRENT_EL,CurrentEL);
        CONTEXT(FX_CONTEXT_DAIF,DAIF);
        CONTEXT(FX_CONTEXT_SPSEL,SPSel);
        case FX_CONTEXT_SP:__asm__ volatile("mov %0,sp":"=r"(value));return value;
        CONTEXT(FX_CONTEXT_COUNTER,cntpct_el0);
        CONTEXT(FX_CONTEXT_FREQUENCY,cntfrq_el0);
        CONTEXT(FX_CONTEXT_TTBR0_EL1,ttbr0_el1);
        CONTEXT(FX_CONTEXT_TTBR1_EL1,ttbr1_el1);
        CONTEXT(FX_CONTEXT_SCTLR_EL1,sctlr_el1);
        CONTEXT(FX_CONTEXT_MAIR_EL1,mair_el1);
        CONTEXT(FX_CONTEXT_TCR_EL1,tcr_el1);
        CONTEXT(FX_CONTEXT_CPACR_EL1,cpacr_el1);
        default:return UINT64_MAX;
    }
#undef CONTEXT
}
static FxResult read_bytes(void *context, uint64_t address, void *out, uint32_t n) {
    (void)context;
    uint8_t *d = out;
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)address;
    for (uint32_t i = 0; i < n; ++i)
        d[i] = p[i];
    return FX_OK;
}
static FxResult write_bytes(void *context, uint64_t address, const void *in, uint32_t n) {
    (void)context;
    const uint8_t *p = in;
    volatile uint8_t *d = (volatile uint8_t *)(uintptr_t)address;
    for (uint32_t i = 0; i < n; ++i)
        d[i] = p[i];
    __asm__ volatile("dsb sy" ::: "memory");
    return FX_OK;
}
static FxResult call(void *context, const FxNamedBinding *binding, const uint64_t a[8], uint64_t *value) {
    (void)context;
    typedef uint64_t (*Fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    *value = ((Fn)(uintptr_t)binding->address)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    return FX_OK;
}
static FxResult scalar(void *context,uint64_t address,uint32_t bytes,uint64_t *value,uint32_t store) {
    (void)context;if(!value || (bytes!=1u && bytes!=2u && bytes!=4u && bytes!=8u) || (address&(bytes-1u)))return FX_BOUNDS;
#define ACCESS(t) do {volatile t *p=(volatile t *)(uintptr_t)address;if(store)*p=(t)*value;else *value=*p;} while(0)
    __asm__ volatile("dmb sy":::"memory");
    if(bytes==1u)ACCESS(uint8_t);else if(bytes==2u)ACCESS(uint16_t);else if(bytes==4u)ACCESS(uint32_t);else ACCESS(uint64_t);
    __asm__ volatile("dsb sy":::"memory");
#undef ACCESS
    return FX_OK;
}
static FxResult cache(void *context,uint64_t address,uint32_t bytes,uint32_t operation) {
    (void)context;
    if(operation==FX_MACHINE_QUERY) {
        if(!address || bytes!=sizeof(FxMachineInfo))return FX_ARGUMENT;
        uint64_t ctr;__asm__ volatile("mrs %0,ctr_el0":"=r"(ctr));
        FxMachineInfo *info=(void *)(uintptr_t)address;
        info->data_line=4u<<(uint32_t)((ctr>>16)&15u);info->instruction_line=4u<<(uint32_t)(ctr&15u);return FX_OK;
    }
    if(operation==FX_MACHINE_BARRIER){__asm__ volatile("dsb sy; isb":::"memory");return FX_OK;}
    if(!bytes || bytes>1024u*1024u || address>UINT64_MAX-bytes ||
       operation<FX_MACHINE_CLEAN || operation>FX_MACHINE_INSTRUCTIONS)return FX_BOUNDS;
    uint64_t ctr;__asm__ volatile("mrs %0,ctr_el0":"=r"(ctr));
    uint64_t line=UINT64_C(4)<<((ctr>>16)&15u),end=address+bytes;
    /* Invalidating partial dirty lines can destroy neighboring bytes. Native
     * DMA buffers must be aligned/owned at the queried data-cache line size. */
    if(operation==FX_MACHINE_INVALIDATE && ((address&(line-1u)) || (end&(line-1u))))return FX_BOUNDS;
    for(uint64_t at=address&~(line-1u);at<end;) {
        if(operation==FX_MACHINE_CLEAN)__asm__ volatile("dc cvac,%0"::"r"(at):"memory");
        else if(operation==FX_MACHINE_INVALIDATE)__asm__ volatile("dc ivac,%0"::"r"(at):"memory");
        else if(operation==FX_MACHINE_CLEAN_INVALIDATE)__asm__ volatile("dc civac,%0"::"r"(at):"memory");
        else __asm__ volatile("dc cvau,%0"::"r"(at):"memory");
        if(at>UINT64_MAX-line)break;at+=line;
    }
    __asm__ volatile("dsb sy":::"memory");
    if(operation==FX_MACHINE_INSTRUCTIONS) {
        line=UINT64_C(4)<<(ctr&15u);
        for(uint64_t at=address&~(line-1u);at<end;) {
            __asm__ volatile("ic ivau,%0"::"r"(at):"memory");
            if(at>UINT64_MAX-line)break;at+=line;
        }
        __asm__ volatile("dsb sy; isb":::"memory");
    }
    return FX_OK;
}
static FxResult resource(void *context,uint32_t op,const FxNamedBinding *b,FxPlatformOwner *owner,uint64_t token,
    uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    if(b->flags&FX_BIND_PROVIDER) {
        typedef FxResult (*Provider)(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);
        return ((Provider)(uintptr_t)b->address)(context,op,b,owner,token,cookie,q,a);
    }
    return fx_native_resource(context,op,b,owner,token,cookie,q,a);
}
static FxResult call_frame(void *context,const FxNamedBinding *binding,FxNativeCallFrame *frame) {
    (void)context;
    if(frame->flags&FX_NATIVE_CALL_SIMD) {
        uint64_t cpacr;__asm__ volatile("mrs %0,cpacr_el1":"=r"(cpacr));
        /* FPEN=01 permits EL1 while trapping EL0, as the module entry gate
         * intentionally does. Both 01 and 11 allow this native EL1 call. */
        if(!(cpacr&(UINT64_C(1)<<20)))return FX_CAPABILITY;
    }
    fx_native_aapcs64(frame,(uintptr_t)binding->address);return FX_OK;
}
FxPlatformIO fx_platform_native_io(void) { return (FxPlatformIO){0, read_bytes, write_bytes, call, resource,scalar,cache,call_frame}; }
#ifdef FX_PLATFORM_PROFILE
#include "platform_bindings.h"
#include "fenestra_native_ui_pager.h"
static const FxPlatform target = {FX_PLATFORM_ABI,
                                  sizeof(fx_target_bindings) / sizeof(fx_target_bindings[0]),
                                  FX_TARGET_MODEL,
                                  FX_TARGET_VERSION,
                                  FX_TARGET_IDENTITY,
                                  fx_target_bindings,
                                  {0, read_bytes, write_bytes, call, resource,scalar,cache,call_frame},
                                  FX_TARGET_MODULE_IDENTITY};
const FxPlatform *fx_platform_target(void) { return &target; }
FxResult fx_platform_capacity_init(FxCapacityRegistry *r,FxNativeCapacityDomains *d,FxIndexMemory memory,FxAllocationObserver observer,FxNativeUiSpace *space) {
    if(!d || !space || !space->domain)return FX_ARGUMENT;
    FxResult result=fx_capacity_init(r,memory,observer);
    if(result==FX_OK)result=fx_capacity_register(r,&d->card,"camera.card.streams.a",FX_TARGET_CARD_STREAM_LIMIT,FX_TARGET_CARD_STREAM_EXHAUSTED);
    if(result==FX_OK)result=fx_capacity_register(r,&d->buffers,"rtos.memory.buffers.bytes",0u,0u);
    if(result==FX_OK)result=fx_capacity_register(r,&d->events,"rtos.events",0u,0u);
    if(result==FX_OK)result=fx_capacity_register(r,&d->mappings,"mmu.claimed-bytes",0u,0u);
    if(result==FX_OK)d->mappings.flags|=FX_CAPACITY_VIEW;
    if(result==FX_OK)result=fx_capacity_register(r,&d->ui_pages,"ui.native.pages",space->limit-space->first,FX_BUDGET);
    if(result==FX_OK)result=fx_capacity_register(r,&d->ui_texts,"ui.native.text.ids",FX_NATIVE_TEXT_LIMIT-space->extra_text_base,FX_BUDGET);
    if(result==FX_OK) {
        d->ui_pages.flags|=FX_CAPACITY_KNOWN_LIMIT;d->ui_texts.flags|=FX_CAPACITY_KNOWN_LIMIT;
        result=fx_native_ui_space_observe(space,&d->ui_pages,&d->ui_texts);
    }
    if(result==FX_OK)fx_capacity_publish(r);
    return result;
}
#endif
