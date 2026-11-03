/* Shared logical .wlm.disabled names use physical .off short names on C2.01.
 * Ordinary discovery does not admit .off packages. No existing destination
 * is replaced, and an uncertain rename closes manager IO until reboot. */
#include "manager.h"
#include "runtime.h"
#include "fenestra_memory.h"
#include "fenestra_platform_native.h"
extern FxZ63Runtime *fx_z63_menu_runtime(void);
#ifndef FX_Z63_NATIVE_CALL
#define FX_Z63_NATIVE_CALL(a,t) ((t)(uintptr_t)(a))
#endif
static int same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static int physical(const char *name,char out[32]) {
    if(!name || !fx_service_name(name,32u,1))return 0;
    uint32_t n=0;while(name[n])++n;
    uint32_t disabled=n>13u && !memcmp(name+n-13u,".wlm.disabled",13u);
    uint32_t suffix=disabled?13u:4u;
    if(n<=suffix || n-suffix>8u || (!disabled && memcmp(name+n-4u,".wlm",4u)))return 0;
    for(uint32_t i=0;i<n-suffix;++i)
        if(!((name[i]>='a' && name[i]<='z') || (name[i]>='0' && name[i]<='9') || name[i]=='_' || name[i]=='-'))return 0;
    memcpy(out,name,n-suffix);memcpy(out+n-suffix,disabled?".off":".wlm",5u);return 1;
}
static FxResult open_directory(void *context) {
    FxZ63Manager *m=context;if(m->uncertain)return FX_CLOSE;
    return m->files.open(m->files.context);
}
static FxResult next_directory(void *context,FxModuleDirectoryEntry *e) {
    FxZ63Manager *m=context;if(m->uncertain)return FX_CLOSE;
    FxResult r=m->files.next(m->files.context,e);if(r!=FX_OK)return r;
    uint32_t n=0;while(n<sizeof(e->name) && e->name[n])++n;
    if(n>4u && n<=12u && !memcmp(e->name+n-4u,".off",4u)) {
        memcpy(e->name+n-4u,".wlm.disabled",14u);
        char checked[32];if(!physical(e->name,checked))e->name[0]=0;
    }
    return FX_OK;
}
static FxResult close_directory(void *context) {
    FxZ63Manager *m=context;return m->files.close(m->files.context);
}
static FxResult read_module(void *context,const char *name,uint8_t *out,uint32_t capacity,uint32_t *bytes) {
    FxZ63Manager *m=context;char file[32]={0};
    if(m->uncertain)return FX_CLOSE;
    if(!physical(name,file))return FX_ARGUMENT;
    return m->files.read(m->files.context,file,out,capacity,bytes);
}
static int32_t open_file(const char *path){return FX_Z63_NATIVE_CALL(0x402e3674u,int32_t (*)(const char *,uint32_t,uint32_t))(path,0u,0u);}
static uint32_t close_file(int32_t fd){return FX_Z63_NATIVE_CALL(0x402e36d4u,uint32_t (*)(int32_t))(fd);}
static FxResult rename_module(void *context,const char *from,const char *to) {
    FxZ63Manager *m=context;char a[64]="A:\\FENESTRA\\",b[64]="A:\\FENESTRA\\";
    if(m->uncertain)return FX_CLOSE;
    if(!physical(from,a+12u) || !physical(to,b+12u))return FX_ARGUMENT;
    uint32_t n=0;while(a[12u+n] && a[12u+n]!='.')++n;
    if(memcmp(a,b,12u+n+1u) || same(a,b))return FX_ARGUMENT;
    int32_t fd=open_file(b);
    if(fd>=0){if(close_file(fd)){m->uncertain=1u;return FX_CLOSE;}return FX_INTEGRITY;}
    if((uint32_t)fd!=UINT32_C(0x80000010))return FX_IO;
    uint32_t r=FX_Z63_NATIVE_CALL(0x402e66ecu,uint32_t (*)(const char *,const char *))(a,b);
    if(r){m->uncertain=1u;return FX_CLOSE;}
    fd=open_file(a);
    if(fd>=0){(void)close_file(fd);m->uncertain=1u;return FX_CLOSE;}
    if((uint32_t)fd!=UINT32_C(0x80000010)){m->uncertain=1u;return FX_CLOSE;}
    fd=open_file(b);if(fd<0){m->uncertain=1u;return FX_CLOSE;}
    if(close_file(fd)){m->uncertain=1u;return FX_CLOSE;}
    return FX_OK;
}
static uint32_t current(void *context,const char *id) {
    (void)context;FxZ63Runtime *runtime=fx_z63_menu_runtime();
    for(uint32_t i=0;i<fx_z63_runtime_count(runtime);++i) {
        const char *loaded=fx_z63_worker_identity(fx_z63_runtime_worker(runtime,i));
        if(loaded && same(loaded,id))return 1u;
    }
    return 0u;
}
uint32_t fx_native_module_loaded(const char *id){return current(0,id);}
static void *index_allocate(void *context,uint32_t n){(void)context;return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u)(n,0u);}
static void index_release(void *context,void *p){(void)context;((void (*)(void *))(uintptr_t)0x40d32d40u)(p);}
FxResult fx_z63_manager_init(FxZ63Manager *m,const FxNativeMenuProfile *profile,void *(*allocate)(uint32_t),uint32_t texts,uint32_t rows,const uintptr_t callbacks[5],int active) {
    if(!m)return FX_ARGUMENT;memset(m,0,sizeof(*m));
    FxResult r=fx_z63_directory_init(&m->directory,&m->files);if(r!=FX_OK)return r;
    FxModuleManagerIO io={{m,open_directory,next_directory,close_directory,read_module},rename_module,m,current,
        {0,index_allocate,index_release},FX_MODULE_INDEX_BUDGET};
    return fx_native_modules_init_io(&m->native,fx_platform_target(),profile,allocate,texts,rows,callbacks,active,&io);
}
