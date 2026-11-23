#include "card_boot_native.h"
#include "card_boot_profile.h"
#include <stdatomic.h>

typedef struct {
    uint32_t site,before,link,gate;
} FxPortRoute;

#include "card_port_routes.h"

#define ROUTE_COUNT (sizeof(fx_card_port_routes)/sizeof(fx_card_port_routes[0]))
#define PHASE_ALIAS (FX_BOOT_ALIAS_ADDRESS+0x60000u)
#define ROUTE_ALIASES (PHASE_ALIAS+FX_BOOT_PAGE)
extern const _Atomic uint32_t fx_card_port_phase;

FxResult fx_card_port_install(void) {
    const uintptr_t phase=(uintptr_t)&fx_card_port_phase;
    const uintptr_t phase_page=phase&~(uintptr_t)(FX_BOOT_PAGE-1u);
    if(atomic_load_explicit(&fx_card_port_phase,memory_order_acquire))return FX_STATE;
    FxResult result=fx_card_boot_map_alias(phase_page,PHASE_ALIAS);
    if(result!=FX_OK)return result;

    uintptr_t aliases[ROUTE_COUNT];
    uint32_t branches[ROUTE_COUNT];
    uintptr_t previous_page=0u,alias=0u;
    size_t mapped=0u;
    for(size_t i=0;i<ROUTE_COUNT;++i) {
        const FxPortRoute *route=&fx_card_port_routes[i];
        uintptr_t page=route->site&~(uintptr_t)(FX_BOOT_PAGE-1u);
        int64_t delta=(int64_t)route->gate-(int64_t)route->site;
        if((delta&3) || delta<-(INT64_C(1)<<27) || delta>=(INT64_C(1)<<27) ||
           *(const volatile uint32_t *)(uintptr_t)route->site!=route->before) {
            result=FX_INTEGRITY;
            break;
        }
        if(page!=previous_page) {
            alias=ROUTE_ALIASES+mapped*FX_BOOT_PAGE;
            result=fx_card_boot_map_alias(page,alias);
            if(result!=FX_OK)break;
            previous_page=page;
            ++mapped;
        }
        aliases[i]=alias+(route->site-page);
        if(*(const volatile uint32_t *)aliases[i]!=route->before) {
            result=FX_INTEGRITY;
            break;
        }
        branches[i]=(route->link?UINT32_C(0x94000000):UINT32_C(0x14000000))|
            ((uint32_t)(delta/4)&UINT32_C(0x3ffffff));
    }
    if(result==FX_OK) {
        for(size_t i=0;i<ROUTE_COUNT;++i)
            atomic_store_explicit((_Atomic uint32_t *)aliases[i],branches[i],memory_order_relaxed);
        for(size_t i=0;i<mapped;++i)fx_card_boot_clean(ROUTE_ALIASES+i*FX_BOOT_PAGE,FX_BOOT_PAGE);
        fx_card_boot_sync_instructions();
    }
    while(mapped) {
        --mapped;
        FxResult cleanup=fx_card_boot_unmap_alias(ROUTE_ALIASES+mapped*FX_BOOT_PAGE);
        if(result==FX_OK)result=cleanup;
    }
    if(result==FX_OK) {
        const uintptr_t address=PHASE_ALIAS+(phase-phase_page);
        atomic_store_explicit((_Atomic uint32_t *)address,1u,memory_order_release);
        fx_card_boot_clean(address,4u);
        fx_card_boot_sync_instructions();
        /* Publication owns these retained routes for the rest of the boot. */
    }
    (void)fx_card_boot_unmap_alias(PHASE_ALIAS);
    return result;
}

void fx_card_port_attach(void) {
#if FX_BOOT_ZR
    extern void fx_c110_menu_system_event(uint32_t,int);
    fx_c110_menu_system_event(FX_BOOT_APP_READY,0);
#else
    extern void fx_z9_card_port_attach(void);
    fx_z9_card_port_attach();
#endif
}
