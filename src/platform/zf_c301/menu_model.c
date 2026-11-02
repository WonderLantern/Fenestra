#include "menu_model.h"
#include "fenestra_native_namespace.h"
#include "fenestra_memory.h"
#if defined(FX_ZF_EXPERIMENTAL_LOADED_MODULES) && FX_ZF_EXPERIMENTAL_LOADED_MODULES == 1
#include "loaded_modules.h"
#endif
static uint32_t __attribute__((aligned(4))) menu_noop(void *p){(void)p;return 1u;}
FxResult fx_zf_menu_prepare(FxZfMenu *s,const FxNativePage *page,const FxNativeEvent *nav,
                            const uintptr_t callbacks[5],const FxZfMenuActions *actions) {
    if(!s || !page || !page->descriptor || !nav || !callbacks || !actions ||
       !actions->activate || !actions->manage || !actions->usb_toggle)return FX_ARGUMENT;
    if(s->setup.descriptor.rows || s->core.page.identity || atomic_load(&s->ready))return FX_BUSY;
    const FxNativeDescriptor *d=(const void *)page->descriptor;
    FxResult r=fx_zf_setup_prepare(&s->setup,d,nav,(uintptr_t)menu_noop);if(r!=FX_OK)return r;
    s->setup_page=*page;s->setup_page.descriptor=(uintptr_t)&s->setup.descriptor;
    /* Both pages retain this profile.  Use the copied Setup storage rather
     * than the caller's temporary source page/row. */
    const FxNativeMenuProfile profile={&s->setup_page,&s->setup.rows[44],nav,
        0x3dcu,0x2aeu,{0x2afu,0x2b4u,0x2c8u,0x2d7u},0x6c6u,672u};
    s->profile=profile;
    const FxNativeEntry core[]={{2900u,0x7ff1u,0,0,actions->activate,profile.stay},
                               {2901u,0x7ff2u,0,1,(uintptr_t)menu_noop,profile.stay},
                               {2909u,0x7ff4u,1,0,actions->manage,FX_NATIVE_MANAGER_PAGE_BASE},
                               {2910u,0x7ff5u,1,1,(uintptr_t)menu_noop,FX_NATIVE_STARTUP_AUTO_PAGE},
                               {2911u,0x7ff6u,1,1,(uintptr_t)menu_noop,FX_NATIVE_STARTUP_GUARD_PAGE},
                               {2918u,0x7ff7u,0,1,actions->usb_toggle,profile.stay},
                               {2902u,0x7ff3u,0,0,(uintptr_t)menu_noop,profile.back}};
    const FxNativeEntry wonder[]={{2903u,0x7fc0u,0,0,(uintptr_t)menu_noop,profile.stay},
                                 {2902u,0x7ff3u,0,0,(uintptr_t)menu_noop,profile.back}};
    r=fx_native_menu_build(&s->core,&profile,FX_NATIVE_FENESTRA_PAGE,2898u,core,7u,callbacks);
    if(r!=FX_OK)return r;
    r=fx_native_menu_build(&s->wonder,&profile,FX_NATIVE_WONDER_PAGE,2899u,wonder,2u,callbacks);
    if(r!=FX_OK)return r;
    FxNativeMenuProfile child=profile;child.back=FX_NATIVE_FENESTRA_PAGE;
    const FxNativeEntry automatic[]={{2912u,0x7fd0u,0,0,actions->auto_on?actions->auto_on:(uintptr_t)menu_noop,FX_NATIVE_FENESTRA_PAGE},
                                    {2913u,0x7fd1u,0,0,actions->auto_off?actions->auto_off:(uintptr_t)menu_noop,FX_NATIVE_FENESTRA_PAGE}};
    const FxNativeEntry guard[]={{2915u,0x7fd2u,0,0,(uintptr_t)menu_noop,FX_NATIVE_FENESTRA_PAGE},
                                {2914u,0x7fd3u,0,0,actions->clear_guard?actions->clear_guard:(uintptr_t)menu_noop,FX_NATIVE_FENESTRA_PAGE}};
    r=fx_native_menu_build(&s->auto_menu,&child,FX_NATIVE_STARTUP_AUTO_PAGE,2910u,automatic,2u,callbacks);
    if(r!=FX_OK)return r;
    return fx_native_menu_build(&s->guard_menu,&child,FX_NATIVE_STARTUP_GUARD_PAGE,2911u,guard,2u,callbacks);
}
int fx_zf_menu_label(int32_t id,const uint16_t **text,uint32_t *bytes) {
    if(!text || !bytes)return 0;
    switch(id) {
#define LABEL(n,s) case n: {static const uint16_t v[]=u##s;*text=v;*bytes=(uint32_t)sizeof(v)-2u;return 1;}
    LABEL(2898,"Fenestra")
    LABEL(2899,"WonderLantern")
    LABEL(2900,"Activate")
    LABEL(2901,"Status")
    LABEL(2902,"Back")
    LABEL(2903,"No loaded modules")
    LABEL(2904,"Auto startup")
    LABEL(2905,"Clear abnormal startup")
    LABEL(2909,"Modules")
    LABEL(2910,"Auto activate")
    LABEL(2911,"Shutdown guard")
    LABEL(2912,"On")
    LABEL(2913,"Off")
    LABEL(2914,"Clear abnormal flag")
    LABEL(2915,"Cancel")
    LABEL(2916,"Normal")
    LABEL(2917,"Abnormal")
    LABEL(2918,"USB Debug")
    LABEL(2919,"Previous")
    LABEL(2920,"Next")
    LABEL(2922,"UI resources unavailable")
    LABEL(2923,"Retry")
#undef LABEL
    default:return 0;
    }
}

int fx_zf_menu_modules_label(const uint16_t **text,uint32_t *bytes) {
    if(!text || !bytes)return 0;
    static const uint16_t unavailable[]=u"Modules unavailable";
    static const uint16_t loaded[]=u"demo-zf loaded";
    static const uint16_t found[]=u"Modules found";
    static const uint16_t empty[]=u"No modules found";
    static const uint16_t scan_failed[]=u"Module scan failed";
#if defined(FX_ZF_EXPERIMENTAL_LOADED_MODULES) && FX_ZF_EXPERIMENTAL_LOADED_MODULES == 1
    if(fx_native_module_loaded("demo-zf")) {*text=loaded;*bytes=(uint32_t)sizeof(loaded)-2u;return 1;}
    FxNativeModules *manager=fx_zf_owner_modules_manager();
    if(!manager){
        *text=unavailable;*bytes=(uint32_t)sizeof(unavailable)-2u;return 1;
    }
    if(manager->manager.error){*text=scan_failed;*bytes=(uint32_t)sizeof(scan_failed)-2u;return 1;}
    if(!manager->manager.scanned){*text=unavailable;*bytes=(uint32_t)sizeof(unavailable)-2u;return 1;}
    if(manager->manager.count){*text=found;*bytes=(uint32_t)sizeof(found)-2u;return 1;}
    *text=empty;*bytes=(uint32_t)sizeof(empty)-2u;return 1;
#else
    (void)loaded;(void)found;(void)empty;(void)scan_failed;
    *text=unavailable;*bytes=(uint32_t)sizeof(unavailable)-2u;return 1;
#endif
}
