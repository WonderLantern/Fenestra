#include <assert.h>
#include <string.h>
#include <stdint.h>
#undef memset
#undef memcpy
#undef memmove
static int32_t native_open(const char *,uint32_t,uint32_t);
static uint32_t native_close(int32_t);
static uint32_t native_rename(const char *,const char *);
#define FX_Z63_NATIVE_CALL(a,t) ((t)((a)==0x402e3674u?(uintptr_t)native_open:(a)==0x402e36d4u?(uintptr_t)native_close:(uintptr_t)native_rename))
#include "../src/platform/z63_c201/manager.c"
static int source=1,destination,fail_close,fail_rename,rename_calls;
static int32_t native_open(const char *p,uint32_t mode,uint32_t flags) {
 assert(!mode && !flags);
 if(!strcmp(p,"A:\\FENESTRA\\demo.wlm"))return source?7:(int32_t)0x80000010u;
 assert(!strcmp(p,"A:\\FENESTRA\\demo.off"));return destination?8:(int32_t)0x80000010u;
}
static uint32_t native_close(int32_t fd){assert(fd==7 || fd==8);return (uint32_t)fail_close;}
static uint32_t native_rename(const char *a,const char *b){
 assert(!strcmp(a,"A:\\FENESTRA\\demo.wlm") && !strcmp(b,"A:\\FENESTRA\\demo.off"));
 ++rename_calls;if(fail_rename)return 1;source=0;destination=1;return 0;
}
int fx_service_name(const char *s,uint32_t n,int dot){(void)dot;return s && strlen(s)<n;}
FxZ63Runtime *fx_z63_menu_runtime(void){return 0;}
uint32_t fx_z63_runtime_count(const FxZ63Runtime *r){(void)r;return 0;}
FxZ63ModuleWorker *fx_z63_runtime_worker(const FxZ63Runtime *r,uint32_t i){(void)r;(void)i;return 0;}
const char *fx_z63_worker_identity(const FxZ63ModuleWorker *w){(void)w;return 0;}
FxResult fx_z63_directory_init(FxZ63Directory *d,FxModuleDirectory *f){(void)d;(void)f;return FX_OK;}
const FxPlatform *fx_platform_target(void){return 0;}
FxResult fx_native_modules_init_io(FxNativeModules *m,const FxPlatform *p,const FxNativeMenuProfile *q,void *(*a)(uint32_t),uint32_t t,uint32_t r,const uintptr_t c[5],int active,const FxModuleManagerIO *io){
 (void)m;(void)p;(void)q;(void)a;(void)t;(void)r;(void)c;(void)active;(void)io;return FX_OK;
}
int main(void){
 char out[32];assert(physical("demo.wlm",out) && !strcmp(out,"demo.wlm"));
 assert(physical("12345678.wlm.disabled",out) && !strcmp(out,"12345678.off"));
 const char *bad[]={"demo.off","123456789.wlm","../demo.wlm","Demo.wlm",".wlm","a.wlm.disabledx"};
 for(uint32_t i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!physical(bad[i],out));
 FxZ63Manager m={0};destination=1;
 assert(rename_module(&m,"demo.wlm","demo.wlm.disabled")==FX_INTEGRITY && !rename_calls && !m.uncertain);
 destination=0;fail_rename=1;
 assert(rename_module(&m,"demo.wlm","demo.wlm.disabled")==FX_CLOSE && m.uncertain && rename_calls==1);
 assert(rename_module(&m,"demo.wlm","demo.wlm.disabled")==FX_CLOSE && rename_calls==1);
 m.uncertain=0;fail_rename=0;
 assert(rename_module(&m,"demo.wlm","demo.wlm.disabled")==FX_OK && !m.uncertain && !source && destination);
 m.uncertain=0;fail_close=1;
 assert(rename_module(&m,"demo.wlm","demo.wlm.disabled")==FX_CLOSE && m.uncertain);
 return 0;
}
