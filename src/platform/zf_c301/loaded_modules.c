#if !defined(FX_ZF_EXPERIMENTAL_LOADED_MODULES) || FX_ZF_EXPERIMENTAL_LOADED_MODULES != 1
#error "Zf resident module registry is experimental and has no unload path"
#endif
#include "loaded_modules.h"
#include "fenestra_native_modules.h"
#include "fenestra_memory.h"
static int overlaps(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    if(!an || !bn)return 0;
    return a>UINTPTR_MAX-an || b>UINTPTR_MAX-bn || (a<b+bn && b<a+an);
}
static int identity(char out[16],const char *id) {
    if(!id)return 0;
    uint32_t n=0;
    for(;n<16u && id[n];n++) {
        uint8_t c=(uint8_t)id[n];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
             (c>='0' && c<='9') || c=='_' || c=='-'))return 0;
        out[n]=(char)c;
    }
    if(!n || n==16u)return 0;
    for(;n<16u;n++)out[n]=0;
    return 1;
}
uint32_t fx_zf_modules_loaded(const FxZfLoadedModules *registry,const char *id) {
    char name[16];if(!registry || !identity(name,id))return 0;
    for(const FxZfLoadedModule *r=atomic_load_explicit(&registry->head,memory_order_acquire);r;r=r->next)
        if(!memcmp(r->id,name,16u))return 1u;
    return 0u;
}
FxResult fx_zf_modules_bind(FxZfLoadedModules *registry,FxZfLoadedModule *record,const char *id,
                            FxPlatformOwner *owner,const FxModule *module,void *image,uint32_t bytes) {
    char name[16];
    if(!registry || !record || !owner || !module || !image || !identity(name,id))return FX_ARGUMENT;
    if(overlaps((uintptr_t)record,sizeof(*record),(uintptr_t)registry,sizeof(*registry)) ||
       overlaps((uintptr_t)record,sizeof(*record),(uintptr_t)owner,sizeof(*owner)) ||
       overlaps((uintptr_t)record,sizeof(*record),(uintptr_t)module,sizeof(*module)) ||
       overlaps((uintptr_t)record,sizeof(*record),(uintptr_t)image,bytes) ||
       overlaps((uintptr_t)registry,sizeof(*registry),(uintptr_t)owner,sizeof(*owner)) ||
       overlaps((uintptr_t)registry,sizeof(*registry),(uintptr_t)image,bytes))return FX_ARGUMENT;
    if(atomic_load_explicit(&record->registry,memory_order_acquire))return FX_BUSY;
    /* Record must be exclusive zeroed storage, disjoint from owner/image.
     * Native callers validate those extents before allocating this controller. */
    if(record->next || record->owner)return FX_ARGUMENT;
    for(unsigned i=0;i<16u;i++)if(record->id[i])return FX_ARGUMENT;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong_explicit(&registry->writer,&zero,1u,
            memory_order_acquire,memory_order_relaxed))return FX_BUSY;
    FxResult result;
    if(fx_zf_modules_loaded(registry,name))result=FX_STALE;
    else {
        result=fx_platform_bind_module(owner,module,image,bytes);
        if(result==FX_OK) {
            memcpy(record->id,name,16u);record->owner=owner;
            record->next=atomic_load_explicit(&registry->head,memory_order_relaxed);
            atomic_store_explicit(&record->registry,(uintptr_t)registry,memory_order_release);
            atomic_store_explicit(&registry->head,record,memory_order_release);
        }
    }
    atomic_store_explicit(&registry->writer,0u,memory_order_release);
    return result;
}
uint32_t fx_native_module_loaded(const char *id){return fx_zf_modules_loaded(fx_zf_owner_modules(),id);}
