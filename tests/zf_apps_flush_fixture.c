#include <assert.h>
#include <string.h>
#include "fenestra_native_apps.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_context.h"
static FxNativeApps apps;
static FxNativeUiDispatch domain;
static FxNativePage page;
static uintptr_t current_task=42u;
static int have_apps=1,have_domain=1,have_context=1,locked;
static FxResult preparation;
static uint32_t destination;
static char calls[16];static unsigned count;
static void enter(void *object){assert(object==(void *)0x1000u && !locked);locked=1;calls[count++]='L';}
static void leave(void *object){assert(object==(void *)0x1000u && locked);locked=0;calls[count++]='U';}
static const FxNativePage *current_page(void){assert(locked);calls[count++]='P';return &page;}
static void navigate(uint32_t id){assert(!locked && id==destination);calls[count++]='N';}
#define FX_ZF_GUI_OBJECT 0x1000u
#define FX_ZF_GUI_ENTER ((uintptr_t)enter)
#define FX_ZF_GUI_LEAVE ((uintptr_t)leave)
#define FX_ZF_GUI_NAVIGATE ((uintptr_t)navigate)
#define FX_ZF_DISPATCH_CURRENT_PAGE ((uintptr_t)current_page)
#include "../src/platform/zf_c301/apps_flush.c"
FxNativeApps *fx_native_apps_domain(void){return have_apps?&apps:0;}
FxNativeUiDispatch *fx_native_ui_dispatch_domain(void){return have_domain?&domain:0;}
uint64_t fx_native_context(uint64_t field,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g){
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;assert(field==FX_CONTEXT_TPIDR_EL1);
    return have_context?(uintptr_t)&current_task:0;
}
FxResult fx_native_apps_prepare_navigation(FxNativeApps *a,const FxNativePage *p,uint32_t *target){
    assert(locked && a==&apps && p==&page);calls[count++]='W';*target=destination;return preparation;
}
static void reset(void){memset(calls,0,sizeof(calls));count=0;assert(!locked);}
int main(void){
    assert(fx_native_apps_flush_native()==FX_EMPTY && !count);
    atomic_store(&apps.navigation_pending,1u);
    assert(fx_native_apps_flush_native()==FX_STATE && !count);
    atomic_store(&domain.task,42u);current_task=43u;
    assert(fx_native_apps_flush_native()==FX_STATE && !count);
    current_task=42u;have_context=0;
    assert(fx_native_apps_flush_native()==FX_STATE && !count);
    have_context=1;have_domain=0;
    assert(fx_native_apps_flush_native()==FX_STATE && !count);
    have_domain=1;preparation=FX_BUSY;destination=123u;
    assert(fx_native_apps_flush_native()==FX_BUSY && !strcmp(calls,"LPWU"));
    reset();preparation=FX_OK;
    assert(fx_native_apps_flush_native()==FX_OK && !strcmp(calls,"LPWUN"));
    reset();destination=0u;
    assert(fx_native_apps_flush_native()==FX_OK && !strcmp(calls,"LPWU"));
    reset();preparation=FX_EMPTY;
    assert(fx_native_apps_flush_native()==FX_EMPTY && !strcmp(calls,"LPWU"));
    reset();have_apps=0;
    assert(fx_native_apps_flush_native()==FX_EMPTY && !count);
    return 0;
}
