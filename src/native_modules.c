#include "fenestra_native_modules.h"
#include "fenestra_native_namespace.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_memory.h"
static uintptr_t function(const FxNativeModules *m,const char *name) {
    const FxPlatform *p=m->manager.ui.platform;
    for(uint32_t i=0;i<p->count;++i) {
        const char *n=p->bindings[i].name;uint32_t j=0;
        while(n[j] && name[j] && n[j]==name[j])++j;
        if(!n[j] && !name[j] && p->bindings[i].kind==FX_BIND_FUNCTION)return (uintptr_t)p->bindings[i].address;
    }
    return 0;
}
static int path(char out[64],const char *file) {
    if(!fx_service_name(file,32u,1))return 0;
    memset(out,0,64u);memcpy(out,"A:\\FENESTRA\\",12u);uint32_t n=0;
    while(n<32u && file[n])++n;
    memcpy(out+12u,file,n+1u);return 1;
}
static FxResult open_directory(void *ctx) {
    FxNativeModules *m=ctx;if(m->poisoned)return FX_CLOSE;
    m->directory=-1;
    uintptr_t fn=function(m,"camera.card.directory.open");if(!fn)return FX_CAPABILITY;
    uint32_t r=((uint32_t (*)(const char *,int32_t *))fn)("A:\\FENESTRA",&m->directory);
    return r==UINT32_C(0x80000010)?FX_EMPTY:r || m->directory<0?FX_IO:FX_OK;
}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static FxResult next_directory(void *ctx,FxModuleDirectoryEntry *out) {
    FxNativeModules *m=ctx;uint8_t data[0x220] __attribute__((aligned(8)))={0};
    uintptr_t fn=function(m,"camera.card.directory.next");if(!fn)return FX_CAPABILITY;
    uint32_t r=((uint32_t (*)(int32_t,void *))fn)(m->directory,data);
    if(r==UINT32_C(0x80000030))return FX_EMPTY;
    if(r)return FX_IO;
    memset(out,0,sizeof(*out));out->attributes=data[0x20e];out->bytes=(uint64_t)u32(data+0x218)|(uint64_t)u32(data+0x21c)<<32;
    for(uint32_t i=0;i<256u;++i) {
        uint32_t ch=(uint32_t)data[14u+2u*i]|(uint32_t)data[15u+2u*i]<<8;
        if(!ch) {
            if(!i)for(uint32_t j=0;j<13u && data[j];++j) {
                if(data[j]>127u){out->name[0]=0;break;}
                out->name[j]=(char)data[j];
            }
            return FX_OK;
        }
        /* Unrelated long/non-ASCII names are ignored, never truncated into
         * a valid module name. Complete native enumeration still advances. */
        if(ch>127u || i>=31u){out->name[0]=0;return FX_OK;}
        out->name[i]=(char)ch;
    }
    out->name[0]=0;return FX_OK;
}
static FxResult close_directory(void *ctx) {
    FxNativeModules *m=ctx;uintptr_t fn=function(m,"camera.card.directory.close");if(!fn)return FX_CAPABILITY;
    uint32_t r=((uint32_t (*)(int32_t))fn)(m->directory);m->directory=-1;
    if(r)m->poisoned=1u;
    return r?FX_CLOSE:FX_OK;
}
static FxResult read_module(void *ctx,const char *file,uint8_t *out,uint32_t capacity,uint32_t *bytes) {
    FxNativeModules *m=ctx;char name[64];*bytes=0;
    if(m->poisoned)return FX_CLOSE;
    if(!path(name,file) || !capacity || capacity>FX_NATIVE_MODULE_PACKAGE_LIMIT+1u)return FX_ARGUMENT;
    uintptr_t op=function(m,"camera.card.open"),rd=function(m,"camera.card.read"),cl=function(m,"camera.card.close");
    if(!op || !rd || !cl)return FX_CAPABILITY;
    int32_t fd=((int32_t (*)(const char *,uint32_t,uint32_t))op)(name,0u,0u);
    FxCapacityClaim claim=fx_file_observe_open(m->file_observer,fd);if(fd<0)return FX_IO;
    uint32_t error=0,calls=0;
    while(*bytes<capacity) {
        uint32_t done=0,room=capacity-*bytes;
        if(++calls>FX_READ_CALL_LIMIT){error=1u;break;}
        error=((uint32_t (*)(int32_t,void *,uint32_t,uint32_t *))rd)(fd,out+*bytes,room,&done);
        if(error || done>room){error=1u;break;}
        *bytes+=done;if(!done)break;
    }
    uint32_t closed=((uint32_t (*)(int32_t))cl)(fd);
    fx_file_observe_close(m->file_observer,&claim,closed);
    if(closed)m->poisoned=1u;
    return closed?FX_CLOSE:error?FX_IO:FX_OK;
}
static FxResult rename_module(void *ctx,const char *from,const char *to) {
    FxNativeModules *m=ctx;char a[64],b[64];
    if(m->poisoned)return FX_CLOSE;
    if(!path(a,from) || !path(b,to))return FX_ARGUMENT;
    uintptr_t op=function(m,"camera.card.open"),cl=function(m,"camera.card.close"),rn=function(m,"camera.card.rename");
    if(!op || !cl || !rn)return FX_CAPABILITY;
    int32_t fd=((int32_t (*)(const char *,uint32_t,uint32_t))op)(b,0u,0u);
    FxCapacityClaim claim=fx_file_observe_open(m->file_observer,fd);
    if(fd>=0) {
        uint32_t closed=((uint32_t (*)(int32_t))cl)(fd);fx_file_observe_close(m->file_observer,&claim,closed);
        if(closed){m->poisoned=1u;return FX_CLOSE;}
        return FX_INTEGRITY; /* Never replace an existing destination. */
    }
    if((uint32_t)fd!=UINT32_C(0x80000010))return FX_IO;
    uint32_t r=((uint32_t (*)(const char *,const char *))rn)(a,b);
    if(r){m->poisoned=1u;return FX_CLOSE;}
    /* Verify the on-card metadata, not just the native return code. */
    fd=((int32_t (*)(const char *,uint32_t,uint32_t))op)(a,0u,0u);
    claim=fx_file_observe_open(m->file_observer,fd);
    if(fd>=0){uint32_t closed=((uint32_t (*)(int32_t))cl)(fd);fx_file_observe_close(m->file_observer,&claim,closed);m->poisoned=1u;return FX_CLOSE;}
    if((uint32_t)fd!=UINT32_C(0x80000010)){m->poisoned=1u;return FX_CLOSE;}
    fd=((int32_t (*)(const char *,uint32_t,uint32_t))op)(b,0u,0u);
    claim=fx_file_observe_open(m->file_observer,fd);
    if(fd<0){m->poisoned=1u;return FX_CLOSE;}
    uint32_t closed=((uint32_t (*)(int32_t))cl)(fd);fx_file_observe_close(m->file_observer,&claim,closed);
    if(closed){m->poisoned=1u;return FX_CLOSE;}
    return FX_OK;
}
static uint32_t current(void *context,const char *id){(void)context;return fx_native_module_loaded(id);}
static void *allocate_index(void *context,uint32_t bytes){return ((FxNativeModules *)context)->allocate_index(bytes);}
static void release_index(void *context,void *allocation){((FxNativeModules *)context)->release_index(allocation);}
FxResult fx_native_modules_init(FxNativeModules *m,const FxPlatform *p,const FxNativeMenuProfile *profile,
    void *(*allocate)(uint32_t),uint32_t texts,uint32_t rows,const uintptr_t callbacks[5],int active) {
    if(!m || !p || !allocate)return FX_ARGUMENT;
    memset(m,0,sizeof(*m));m->directory=-1;m->allocate_index=allocate;
    for(uint32_t i=0;i<p->count;++i)if(p->bindings[i].kind==FX_BIND_FUNCTION && p->bindings[i].abi==FX_CALL_U64_8 &&
        !memcmp(p->bindings[i].name,"rtos.memory.release",20u))
        m->release_index=(void (*)(void *))(uintptr_t)p->bindings[i].address;
    if(!m->release_index)return FX_CAPABILITY;
    FxModuleManagerIO io={{m,open_directory,next_directory,close_directory,read_module},rename_module,m,current,
        {m,allocate_index,release_index},FX_MODULE_INDEX_BUDGET};
    FxResult r=fx_module_manager_init(&m->manager,p,&io,active);if(r!=FX_OK)return r;
    r=fx_native_ui_init(&m->ui,&m->manager.ui,profile,allocate,FX_NATIVE_MANAGER_PAGE_BASE,texts,rows,
        fx_native_ui_actions,fx_native_ui_options,callbacks);
    if(r==FX_OK)m->ui.input_ready=fx_native_modules_input;
    return r;
}
FxResult fx_native_modules_open(FxNativeModules *m){
    if(!m)return FX_ARGUMENT;
    FxResult r=fx_module_manager_scan(&m->manager);
    fx_native_ui_refresh_owner(&m->manager.ui);return r;
}
void fx_native_modules_input(FxNativeUi *ui) {
    if(!ui)return;
    FxNativeModules *m=(FxNativeModules *)((uint8_t *)ui - offsetof(FxNativeModules,ui));
    fx_module_manager_tick(&m->manager);
    fx_native_ui_refresh_owner(&m->manager.ui);
}
/* Read-only layout for host observation; no function, address or result is
 * written by the emulator. The production menu does not consume this table. */
const uint32_t fx_native_modules_metadata[]={0x46584d47u,2u,sizeof(FxNativeModules),
    offsetof(FxModuleManager,entries),offsetof(FxModuleManager,count),offsetof(FxModuleManager,scanned),
    offsetof(FxModuleManager,error),offsetof(FxModuleManager,activated),offsetof(FxModuleManager,loading),
    sizeof(FxManagedModule),offsetof(FxManagedModule,present),offsetof(FxManagedModule,enabled),
    offsetof(FxManagedModule,current),offsetof(FxManagedModule,valid),offsetof(FxManagedModule,file),
    offsetof(FxPlatformOwner,control_count),offsetof(FxPlatformOwner,controls),offsetof(FxPlatformOwner,owner),
    sizeof(FxUiControl),offsetof(FxUiControl,label),offsetof(FxUiControl,text),offsetof(FxUiControl,kind)};
