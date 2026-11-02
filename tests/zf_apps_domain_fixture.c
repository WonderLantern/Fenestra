#include <assert.h>
#include <stdlib.h>
#include "apps_domain.h"
static FxZfAppsDomain *owner;
FxZfAppsDomain *fx_zf_owner_apps_domain(void){return owner;}
static void *allocate(uint32_t bytes){return malloc(bytes);}
static uint32_t count(void *context){(void)context;return 0;}
static FxResult entry(void *c,uint32_t i,const FxNativePage *p,int s,FxNativeAppEntry *e){
    (void)c;(void)i;(void)p;(void)s;(void)e;return FX_EMPTY;
}
static FxResult action(void *c,uint32_t i){(void)c;(void)i;return FX_EMPTY;}
static void capture(void){}
static void init(FxNativeApps *apps,const FxNativeMenuProfile *profile,const uintptr_t callbacks[5]) {
    assert(fx_native_apps_init(apps,profile,callbacks,0,allocate,count,entry,action,
        2898u,2899u,2900u,2901u,2902u,2903u,2904u)==FX_OK);
    apps->capture_task=capture;
}
int main(void) {
    FxZfAppsDomain domain={0};FxNativeApps apps={0},other={0};
    FxNativePage page={0};FxNativeRow row={0};FxNativeEvent events[18]={0};
    FxNativeMenuProfile profile={&page,&row,events,0x3dcu,0x2aeu,{0x2afu,0x2b4u,0x2c8u,0x2d7u},0x6c6u,672u};
    const uintptr_t callbacks[5]={1,2,3,4,5};
    assert(!fx_native_apps_domain());owner=&domain;
    assert(fx_zf_apps_publish(&domain,&apps)==FX_STATE && !fx_native_apps_domain());
    init(&apps,&profile,callbacks);
    apps.capture_task=0;
    assert(fx_zf_apps_publish(&domain,&apps)==FX_STATE && !fx_native_apps_domain());
    apps.capture_task=capture;apps.profile.select_events[2]=0x2b1u;
    assert(fx_zf_apps_publish(&domain,&apps)==FX_STATE);
    apps.profile=profile;
    assert(fx_zf_apps_publish(&domain,&apps)==FX_OK && fx_native_apps_domain()==&apps);
    assert(apps.profile.page==&page && apps.count(0)==0 && !apps.root.native.page.identity);
    assert(fx_zf_apps_publish(&domain,&apps)==FX_OK);
    init(&other,&profile,callbacks);
    assert(fx_zf_apps_publish(&domain,&other)==FX_STALE && fx_native_apps_domain()==&apps);
    return 0;
}
