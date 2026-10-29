/* Original RTOS heap backing; no private module mapping or Flash mutation.
 * Serialize with the other managed MMU writers. Original independent table
 * writers remain an external platform constraint, as for rtos.mappings. */
#include "fenestra_hooks.h"
#include "fenestra_machine.h"
#include "fenestra_memory.h"
#include "fenestra_native_mapping.h"
#include "fenestra_mmu_context.h"
static uintptr_t hook_function(FxPlatformOwner *o,const char *name) {
    const FxNamedBinding *b=fx_platform_find(o,name);
    return b && b->kind==FX_BIND_FUNCTION && b->abi==FX_CALL_U64_8?(uintptr_t)b->address:0u;
}
FxResult fx_native_hook_page(FxPlatformOwner *o,uint8_t *code,FxHookCell *cells,uint32_t *retained) {
    *retained=0u;
    const FxNamedBinding *binding=fx_platform_find(o,"rtos.mmu.context");
    uintptr_t protect=hook_function(o,"rtos.mmu.context.protect"),attributes=hook_function(o,"rtos.mmu.context.attributes"),
        translate=hook_function(o,"rtos.mmu.context.translate"),invalidate=hook_function(o,"rtos.mmu.invalidate-all");
    FxNativeMappingRegistry *registry=fx_native_mappings();
    if(!binding || binding->kind!=FX_BIND_TABLE || binding->bytes<sizeof(FxNativeMmuContext) ||
       !protect || !attributes || !translate || !invalidate || !o->platform->io.cache || !registry)return FX_CAPABILITY;
    if((uintptr_t)code&4095u)return FX_ARGUMENT;
    FxResult result;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong_explicit(&registry->lock,&zero,1u,memory_order_acquire,memory_order_relaxed))return FX_BUSY;
    FxNativeMmuContext *source=(void *)(uintptr_t)binding->address,view;
    uintptr_t roots[4]={source->root,0u,0u,0u};uint32_t count=1u,old[4]={0u},changed=0u;
    uint64_t physical[4]={0u};
    if(source->private_roots)for(uint32_t i=0;i<3u;++i) {
        uintptr_t root=source->roots[i];uint32_t duplicate=0u;
        for(uint32_t j=0;j<count;++j)if(roots[j]==root)duplicate=1u;
        if(!duplicate)roots[count++]=root;
    }
    result=FX_STATE;
    /* Bound only the temporary allocator view, not the stock context. */
    for(uint32_t i=0;i<count;++i) {
        if(!roots[i] || (roots[i]&4095u))goto done;
        view=*source;view.root=roots[i];view.private_roots=0u;view.pages=fx_native_mmu_view_pages(source);
        if(((int32_t (*)(void *,uint64_t,uint64_t *,int32_t))translate)(&view,(uintptr_t)code,&physical[i],-1) ||
           ((int32_t (*)(void *,uint64_t,uint32_t *,int32_t))attributes)(&view,(uintptr_t)code,&old[i],-1) ||
           (i && physical[i]!=physical[0]))goto done;
        /* Original bit 0 = read-only; bit 1 = execute. */
        if(old[i]&1u)goto done;
    }
    result=FX_IO;
    /* Some original heap views permit execution. Remove it on every root
     * BEFORE writing any code, then verify the RW/XN transition. */
    for(uint32_t i=0;i<count;++i) {
        changed=i+1u;view=*source;view.root=roots[i];view.private_roots=0u;view.pages=fx_native_mmu_view_pages(source);
        int32_t status=((int32_t (*)(void *,uint64_t,uint64_t,uint32_t,uint32_t,int32_t))protect)(
            &view,(uintptr_t)code,4096u,old[i]&~3u,0u,-1);
        memcpy(source->allocated,view.allocated,sizeof(source->allocated));
        uint32_t actual=0u;
        if(status || ((int32_t (*)(void *,uint64_t,uint32_t *,int32_t))attributes)(&view,(uintptr_t)code,&actual,-1) || actual!=(old[i]&~3u))goto rollback;
    }
    ((void (*)(void))invalidate)();
    fx_hook_page_encode(code,cells);
    result=o->platform->io.cache(o->platform->io.context,(uintptr_t)code,4096u,FX_MACHINE_INSTRUCTIONS);
    if(result!=FX_OK)goto rollback;
    result=FX_IO;
    for(uint32_t i=0;i<count;++i) {
        view=*source;view.root=roots[i];view.private_roots=0u;view.pages=fx_native_mmu_view_pages(source);
        int32_t status=((int32_t (*)(void *,uint64_t,uint64_t,uint32_t,uint32_t,int32_t))protect)(
            &view,(uintptr_t)code,4096u,old[i]|3u,0u,-1);
        memcpy(source->allocated,view.allocated,sizeof(source->allocated));
        uint32_t actual=0u;uint64_t pa=0u;
        if(status || ((int32_t (*)(void *,uint64_t,uint32_t *,int32_t))attributes)(&view,(uintptr_t)code,&actual,-1) ||
           ((int32_t (*)(void *,uint64_t,uint64_t *,int32_t))translate)(&view,(uintptr_t)code,&pa,-1) ||
           actual!=(old[i]|3u) || pa!=physical[i])goto rollback;
    }
    ((void (*)(void))invalidate)();result=FX_OK;goto done;
rollback:
    for(uint32_t i=0;i<changed;++i) {
        view=*source;view.root=roots[i];view.private_roots=0u;view.pages=fx_native_mmu_view_pages(source);
        int32_t status=((int32_t (*)(void *,uint64_t,uint64_t,uint32_t,uint32_t,int32_t))protect)(
            &view,(uintptr_t)code,4096u,old[i],0u,-1);
        memcpy(source->allocated,view.allocated,sizeof(source->allocated));
        uint32_t actual=0u;
        if(status || ((int32_t (*)(void *,uint64_t,uint32_t *,int32_t))attributes)(&view,(uintptr_t)code,&actual,-1) || actual!=old[i])*retained=1u;
    }
    ((void (*)(void))invalidate)();
done:
    atomic_store_explicit(&registry->lock,0u,memory_order_release);return result;
}
