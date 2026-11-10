#include "menu_model.h"
#include "fenestra_native_namespace.h"
#include <assert.h>
#include <string.h>
int main(void) {
    FxZfMenu model={0};FxNativeRow rows[47]={0};uintptr_t events[47]={0};
    uint8_t header[272]={0};uintptr_t callbacks[5]={0x1000,0x1004,0x1008,0x100c,0x1010};
    FxNativeDescriptor stock={.flags=0x80001002,.header=(uintptr_t)header,.rows=(uintptr_t)rows,
        .events=(uintptr_t)events,.count=47};
    FxNativePage page={.identity=0x02a00002,.descriptor=(uintptr_t)&stock,.callbacks=(uintptr_t)callbacks,
        .previous=0x12340000};
    FxNativeEvent nav[18]={0};
    const FxZfMenuActions actions={0x4000,0x5000,0x6000,0x7000,0x8000,0x9000};
    const uint32_t ids[]={0x2a7,0x2a8,0x2ac,0x2ad,0x2ae,0x2af,0x2b4,0x2c6,0x2c7,0x2c8,0x2c9,0x2d7,0x2aa,0x32f,0x3dc};
    for(unsigned i=0;i<15;i++)nav[i]=(FxNativeEvent){ids[i],0x2000,0x3000,0x4000};
    rows[0].word[0]=618;rows[44].word[0]=866;
    assert(fx_zf_menu_prepare(&model,&page,nav,callbacks,&actions)==FX_OK);
    assert(!atomic_load(&model.ready));assert(model.setup.descriptor.count==49);
    assert(model.setup_page.descriptor==(uintptr_t)&model.setup.descriptor);
    assert(model.setup_page.identity==page.identity);
    assert(model.core.page.identity==0x70000002 && model.wonder.page.identity==0x70010002);
    assert(model.core.page.previous==page.previous && model.core.callbacks[2]==callbacks[2]);
    assert(model.core.descriptor.count==7 && model.wonder.descriptor.count==2);
    const uint32_t destinations[]={0x6c6,0x6c6,FX_NATIVE_MANAGER_PAGE_BASE,FX_NATIVE_STARTUP_AUTO_PAGE,FX_NATIVE_STARTUP_GUARD_PAGE,0x6c6,672};
    const uintptr_t handlers[]={0x4000,model.core.handlers[1][5].callback,0x5000,model.core.handlers[3][5].callback,model.core.handlers[4][5].callback,0x6000,model.core.handlers[6][5].callback};
    for(unsigned i=0;i<7;i++)for(unsigned j=0;j<15;j++) {
        FxNativeEvent e=model.core.handlers[i][j];
        if(ids[j]==0x2ae)assert(e.next==672 && !e.condition);
        if(ids[j]==0x2af)assert(e.next==destinations[i] && !e.condition && e.callback==handlers[i]);
    }
    assert(model.auto_menu.page.identity==(FX_NATIVE_STARTUP_AUTO_PAGE<<16|2u));
    assert(model.guard_menu.page.identity==(FX_NATIVE_STARTUP_GUARD_PAGE<<16|2u));
    assert(model.auto_menu.descriptor.count==2 && model.guard_menu.descriptor.count==2);
    assert(model.auto_menu.handlers[0][5].callback==actions.auto_on);
    assert(model.auto_menu.handlers[1][5].callback==actions.auto_off);
    assert(model.guard_menu.handlers[1][5].callback==actions.clear_guard);
    for(unsigned i=0;i<2;i++) {
        assert(model.auto_menu.handlers[i][5].next==FX_NATIVE_FENESTRA_PAGE);
        assert(model.guard_menu.handlers[i][5].next==FX_NATIVE_FENESTRA_PAGE);
    }
    assert(stock.count==47 && stock.rows==(uintptr_t)rows && page.descriptor==(uintptr_t)&stock);
    const uint16_t *text=0;uint32_t bytes=0;
    for(int32_t i=2898;i<2903;i++){assert(fx_zf_menu_label(i,&text,&bytes));assert(text && bytes && !(bytes&1));}
    assert(!fx_zf_menu_label(2897,&text,&bytes) && !fx_zf_menu_label(2924,&text,&bytes));
    assert(!fx_zf_menu_label(2898,0,&bytes));
    assert(fx_zf_menu_label(2900,&text,&bytes));
    assert(bytes==sizeof(u"Activate")-2u && !memcmp(text,u"Activate",bytes));
    assert(FX_ZF_MANAGER_TEXT_BASE>=2898u+FX_ZF_MENU_LABELS);
    assert(FX_ZF_MODULE_TEXT_BASE>=FX_ZF_MANAGER_TEXT_BASE+FX_NATIVE_UI_TEXTS);
    assert(fx_zf_menu_prepare(&model,&page,nav,callbacks,&actions)==FX_BUSY);
    return 0;
}
