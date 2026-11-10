#include <assert.h>
#include <string.h>
#include "loaded_modules.h"
#include "fenestra_native_modules.h"
#include "services.h"
#include "fenestra_native_ui_dispatch.h"
static FxZfLoadedModules *published;
static FxNativeUiDispatch dispatch;
FxNativeUiDispatch *fx_native_ui_dispatch_domain(void){return &dispatch;}
FxZfLoadedModules *fx_zf_owner_modules(void){return published;}
const FxPlatform *fx_platform_target(void){return 0;}
void fx_native_ui_refresh_owner(FxPlatformOwner *owner){(void)owner;}
int main(void) {
    FxZfLoadedModules registry={0};FxZfLoadedModule first={0},second={0},bad={0};
    FxPlatform platform={0};FxPlatformOwner a={0},b={0};
    a.platform=b.platform=&platform;a.owner=1u;b.owner=2u;
    atomic_store(&a.phase,FX_OWNER_RUNNING);atomic_store(&b.phase,FX_OWNER_RUNNING);
    uint8_t image[64]={0},other[64]={0};
    FxModule module={0};module.bytes=64u;module.segment_count=1u;
    module.segments[0]=(FxSegment){0,0,0,64,FX_READ,1};
    assert(!fx_native_module_loaded("demo"));published=&registry;
    assert(fx_zf_modules_bind(&registry,&bad,"demo",&a,&module,image,sizeof(image))==FX_ARGUMENT);
    assert(!a.module_image && !atomic_load(&registry.head) && !atomic_load(&bad.registry));
    module.prepared=1u;
    assert(fx_zf_modules_bind(&registry,&first,"demo",&a,&module,image,sizeof(image))==FX_OK);
    assert(a.module_image==image && a.module_bytes==64u);
    assert(fx_native_module_loaded("demo") && !fx_native_module_loaded("dem"));
    assert(fx_zf_modules_bind(&registry,&second,"demo",&b,&module,other,sizeof(other))==FX_STALE);
    assert(!b.module_image && !atomic_load(&second.registry));
    assert(fx_zf_modules_bind(&registry,&first,"other",&b,&module,other,sizeof(other))==FX_BUSY);
    assert(fx_zf_modules_bind(&registry,&second,"bad/name",&b,&module,other,sizeof(other))==FX_ARGUMENT);
    assert(fx_zf_modules_bind(&registry,&second,"abcdefghijklmnop",&b,&module,other,sizeof(other))==FX_ARGUMENT);
    atomic_store(&registry.writer,1u);
    assert(fx_zf_modules_bind(&registry,&second,"second",&b,&module,other,sizeof(other))==FX_BUSY);
    atomic_store(&registry.writer,0u);
    assert(fx_zf_modules_bind(&registry,&second,"second",&b,&module,other,sizeof(other))==FX_OK);
    atomic_store(&a.phase,FX_OWNER_FAULT);
    assert(fx_native_module_loaded("demo") && fx_native_module_loaded("second"));
    assert(!strcmp(first.id,"demo") && !strcmp(second.id,"second"));
    FxZfServices service={0};uint8_t third[64]={0};
    service.platform.platform=&platform;service.platform.owner=3u;
    atomic_store(&service.platform.phase,FX_OWNER_RUNNING);
    assert(fx_zf_services_bind_module(&service,"third",&module,third,sizeof(third))==FX_OK);
    assert(fx_native_module_loaded("third"));
    assert(fx_zf_services_init(&service,3u,1u,0u)==FX_STATE);
    assert(service.platform.module_image==third && fx_native_module_loaded("third"));
    return 0;
}
