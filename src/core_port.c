#include "fenestra_core_port.h"
#include "fenestra_memory.h"
#include "fenestra_native_display.h"
#include "fenestra_native_linux.h"
#include "fenestra_native_ptp.h"
#include "fenestra_native_rtos.h"
#include "fenestra_native_ui_dispatch.h"

static int native_entry(uint64_t address,uintptr_t base,uint32_t bytes) {
    return !(address&3u) && address>=base+sizeof(FxCorePort)+4u && address<=base+bytes-4u;
}

FxResult fx_core_port_validate(const void *data,uint32_t bytes,uintptr_t base,
                               const uint8_t application[32],const uint8_t core[32]) {
    if(!data || !application || !core || !base || (base&7u) ||
       bytes<sizeof(FxCorePort)+8u || bytes>FX_CARD_PAYLOAD_CAPACITY || base>UINTPTR_MAX-bytes)
        return FX_ARGUMENT;
    FxCorePort port;
    memcpy(&port,data,sizeof(port));
    if(port.magic!=FX_CORE_PORT_MAGIC || port.abi!=FX_CORE_PORT_ABI ||
       port.header_bytes!=sizeof(port) || port.reserved ||
       memcmp(port.application,application,32u) || memcmp(port.core,core,32u))
        return FX_INTEGRITY;
    if(!native_entry(port.install,base,bytes) || !native_entry(port.attach,base,bytes) ||
       !native_entry(port.dispatch,base,bytes))return FX_BOUNDS;
    #define PROFILE_SIZE(object,accessor,type,slot) [slot]=sizeof(type),
    static const uint16_t profile_sizes[FX_CORE_PORT_IMPORT_COUNT]={
        FX_CORE_PORT_PROFILES(PROFILE_SIZE)
    };
    #undef PROFILE_SIZE
    for(uint32_t i=0;i<FX_CORE_PORT_IMPORT_COUNT;++i) {
        uint64_t pointer=port.imports[i];
        uint32_t size=profile_sizes[i];
        if(!native_entry(pointer,base,bytes) ||
           (size && ((pointer&7u) || size>base+bytes-pointer)))return FX_BOUNDS;
    }
    return FX_OK;
}

#if defined(FX_UNIVERSAL_CORE)
extern const FxCorePort __fx_native_port;

typedef struct {
    const FxCardPayloadBoot *boot;
    uint8_t digest[32];
} CoreLoad;

static FxResult publish(void *context,const void *data,uint32_t bytes) {
    const CoreLoad *load=context;
    uintptr_t base=(uintptr_t)&__fx_native_port;
    FxResult result=fx_core_port_validate(data,bytes,base,load->boot->application,load->digest);
    if(result==FX_OK)result=load->boot->copy((uint32_t)(base-FX_CARD_PAYLOAD_ADDRESS),data,bytes);
    if(result==FX_OK)result=((FxResult (*)(void))(uintptr_t)__fx_native_port.install)();
    if(result==FX_OK)((void (*)(void))(uintptr_t)__fx_native_port.attach)();
    return result;
}

void fx_universal_core_start(const FxCardPayloadBoot *boot) {
    uint32_t offset=(uint32_t)((uintptr_t)&__fx_native_port-FX_CARD_PAYLOAD_ADDRESS);
    if(!boot || boot->abi!=FX_CARD_PAYLOAD_ABI || boot->bytes<sizeof(*boot) ||
       !boot->copy || !boot->application || !boot->target || !boot->target[0] ||
       !boot->image_bytes || boot->image_bytes>offset || boot->capacity<=offset)return;
    #define PORT_DIRECTORY "A:\\FENESTRA\\PORTS\\"
    char path[64]=PORT_DIRECTORY;
    uint32_t end=sizeof(PORT_DIRECTORY)-1u;
    #undef PORT_DIRECTORY
    for(const char *p=boot->target;*p;++p) {
        if(end>sizeof(path)-sizeof(".BIN")-1u)return;
        path[end++]=*p;
    }
    memcpy(path+end,".BIN",sizeof(".BIN"));
    CoreLoad load={.boot=boot};
    fx_sha256((const void *)(uintptr_t)FX_CARD_PAYLOAD_ADDRESS,boot->image_bytes,load.digest);
    (void)fx_card_boot_read(boot->io,path,sizeof(FxCorePort)+8u,boot->capacity-offset,
                           25u,5000u,publish,&load);
}
#endif
