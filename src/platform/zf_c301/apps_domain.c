#if !defined(FX_ZF_EXPERIMENTAL_APPS_DOMAIN) || FX_ZF_EXPERIMENTAL_APPS_DOMAIN != 1
#error "Zf Apps controller publication is experimental; no menu pages installed"
#endif
#include "apps_domain.h"
FxNativeApps *fx_zf_apps_get(const FxZfAppsDomain *domain) {
    return domain?atomic_load_explicit(&domain->published,memory_order_acquire):0;
}
FxResult fx_zf_apps_publish(FxZfAppsDomain *domain,FxNativeApps *apps) {
    if(!domain || !apps || (uintptr_t)domain%_Alignof(FxZfAppsDomain) ||
       (uintptr_t)apps%_Alignof(FxNativeApps))return FX_ARGUMENT;
    FxNativeApps *current=fx_zf_apps_get(domain);
    if(current)return current==apps?FX_OK:FX_STALE;
    if(!apps->allocate || !apps->count || !apps->entry || !apps->action ||
       !apps->capture_task || !apps->profile.page || !apps->profile.row ||
       !apps->profile.navigation || !apps->unavailable_text || !apps->retry_text ||
       apps->profile.terminal!=0x3dcu || apps->profile.back_event!=0x2aeu ||
       apps->profile.select_events[0]!=0x2afu || apps->profile.select_events[1]!=0x2b4u ||
       apps->profile.select_events[2]!=0x2c8u || apps->profile.select_events[3]!=0x2d7u)
        return FX_STATE;
    for(unsigned i=0;i<5u;i++)if(!apps->callbacks[i])return FX_STATE;
    /* No controller mutation is performed here. The initializer remains the
     * sole GUI writer; release publication makes its existing setup visible. */
    FxNativeApps *expected=0;
    if(atomic_compare_exchange_strong_explicit(&domain->published,&expected,apps,
            memory_order_acq_rel,memory_order_acquire))return FX_OK;
    return expected==apps?FX_OK:FX_STALE;
}
FxNativeApps *fx_native_apps_domain(void){return fx_zf_apps_get(fx_zf_owner_apps_domain());}
