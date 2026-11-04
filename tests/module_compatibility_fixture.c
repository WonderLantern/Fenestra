/* Read-only synthetic packages; no camera, firmware, mapping or entry calls. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
/* Use the ISO C file API in this fixture on every host compiler. */
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "fenestra_module_manager.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t identity[32];
static uint32_t lookups,native_calls,fail_read,fail_close,position,renames;
static uint32_t many;
static FxNamedBinding bindings[]={
    {"test.output",FX_BIND_DEVICE,FX_CALL_DEVICE,1u,FX_BIND_PROVIDER,FX_SDK_BINDING,0u,1u,{0}},
    {"test.function",FX_BIND_FUNCTION,FX_CALL_U64_8,2u,0u,FX_SDK_NATIVE,0u,1u,{0}},
    {"test.table",FX_BIND_TABLE,FX_CALL_BYTES,3u,FX_BIND_READ,FX_SDK_BINDING,16u,1u,{0}}
};
static FxResult resource(void *c,uint32_t op,const FxNamedBinding *b,FxPlatformOwner *o,uint64_t token,uint64_t *cookie,
    const FxServiceRequest *q,FxServiceReply *a) {
    (void)c;(void)op;(void)b;(void)o;(void)token;(void)cookie;(void)q;(void)a;++native_calls;return FX_IO;
}
static FxResult native_call(void *c,const FxNamedBinding *b,const uint64_t args[8],uint64_t *out) {
    (void)c;(void)b;(void)args;(void)out;++native_calls;return FX_IO;
}
static FxResult native_read(void *c,uint64_t at,void *out,uint32_t n) {
    (void)c;(void)at;(void)out;(void)n;++native_calls;return FX_IO;
}
static FxPlatform platform={.abi=1u,.count=3u,.model="synthetic",.firmware="compatibility",
    .bindings=bindings,.io={.call=native_call,.resource=resource,.read=native_read}};
static FxResult lookup(const void *context,const char *name,FxModuleInterface *out) {
    ++lookups;
    if(many && strlen(name)==8u && !memcmp(name,"test.",5u)) {
        *out=(FxModuleInterface){4u,4u,1u,1u,FX_SDK_BINDING};return FX_OK;
    }
    return fx_platform_interface(context,name,out);
}
static FxModuleEnvironment environment={1u,&platform,lookup,identity};
const FxModuleEnvironment *fx_platform_module_environment(void){return &environment;}
static uint8_t *read_file(const char *name,uint32_t *bytes) {
    FILE *f=fopen(name,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<=1048576);
    assert(!fseek(f,0,SEEK_SET));uint8_t *data=malloc((size_t)n);assert(data && fread(data,1u,(size_t)n,f)==(size_t)n);
    assert(!fclose(f));*bytes=(uint32_t)n;return data;
}
static void quoted(const char *s) {
    putchar('"');for(uint32_t i=0u;s[i];++i){if(s[i]=='"' || s[i]=='\\')putchar('\\');putchar(s[i]);}putchar('"');
}
static void report(const FxModuleIssue *i,FxResult r) {
    printf("{\"result\":%u,\"reason\":%u,\"expected\":%" PRIu64 ",\"actual\":%" PRIu64 ",\"lookups\":%u,\"binding\":",
        (unsigned)r,i->reason,i->expected,i->actual,lookups);quoted(i->binding);puts("}");
}
typedef struct {char name[32];uint8_t *data;uint32_t bytes;} CardFile;
static CardFile files[3];
static FxResult open_card(void *c){(void)c;position=0u;return FX_OK;}
static FxResult next_file(void *c,FxModuleDirectoryEntry *out) {
    (void)c;if(position==3u)return FX_EMPTY;memset(out,0,sizeof(*out));
    memcpy(out->name,files[position].name,32u);out->bytes=files[position++].bytes;return FX_OK;
}
static FxResult close_card(void *c){(void)c;return fail_close?FX_CLOSE:FX_OK;}
static FxResult read_card(void *c,const char *name,uint8_t *out,uint32_t cap,uint32_t *bytes) {
    (void)c;if(fail_read)return FX_IO;
    for(uint32_t i=0u;i<3u;++i)if(!strcmp(files[i].name,name)) {
        assert(cap>files[i].bytes);memcpy(out,files[i].data,files[i].bytes);*bytes=files[i].bytes;return FX_OK;
    }
    return FX_IO;
}
static FxResult rename_card(void *c,const char *before,const char *after) {
    (void)c;for(uint32_t i=0u;i<3u;++i)if(!strcmp(files[i].name,before)) {
        assert(strlen(after)<32u);memset(files[i].name,0,32u);memcpy(files[i].name,after,strlen(after));++renames;return FX_OK;
    }
    return FX_IO;
}
static void *allocate(void *c,uint32_t n){(void)c;return malloc(n);}
static void release(void *c,void *p){(void)c;free(p);}
static int inventory(int argc,char **argv) {
    assert(argc==5);
    const char *names[]={"good.wlm","wrong.wlm","missing.wlm"};
    for(uint32_t i=0u;i<3u;++i){memcpy(files[i].name,names[i],strlen(names[i]));files[i].data=read_file(argv[i+2u],&files[i].bytes);}
    FxModuleDirectory card={0,open_card,next_file,close_card,read_card};
    FxIndexMemory memory={0,allocate,release};FxModuleInventory found={0};static uint8_t scratch[65536];
    assert(fx_modules_discover(&card,identity,FX_SDK_CAPS,scratch,sizeof(scratch),&found,memory)==FX_OK);
    assert(found.count==1u && found.rejected==2u && !strcmp(found.entries[0].id,"good"));fx_modules_destroy(&found);
    static FxModuleManager manager;FxModuleManagerIO io={.directory=card,.rename=rename_card,.memory=memory};
    assert(fx_module_manager_init(&manager,&platform,&io,0)==FX_OK);
    assert(fx_module_manager_scan(&manager)==FX_OK && manager.count==3u);
    assert(manager.entries[0].valid && !manager.entries[0].issue.reason);
    assert(manager.entries[1].issue.reason==FX_MODULE_TARGET && manager.entries[1].error==FX_CAPABILITY);
    assert(manager.entries[2].issue.reason==FX_MODULE_INTERFACE_MISSING && manager.entries[2].error==FX_CAPABILITY);
    FxUiControl controls[16];uint32_t count,revision;
    assert(fx_ui_snapshot(&manager.ui,controls,16u,&count,&revision)==FX_OK);
    uint32_t wrong=0u,missing=0u;
    for(uint32_t i=0u;i<count;++i) {
        if(controls[i].id==5u)wrong=!strcmp(controls[i].text,"Wrong camera or firmware");
        if(controls[i].id==6u)missing=!strcmp(controls[i].text,"Missing: absent.output");
    }
    assert(wrong && missing && !renames && !native_calls);
    assert(fx_module_manager_enable(&manager,2u,1u)==FX_CAPABILITY && !renames);
    assert(fx_module_manager_enable(&manager,1u,0u)==FX_OK && renames==1u);
    assert(!strcmp(files[1].name,"wrong.wlm.disabled"));
    fail_read=1u;
    assert(fx_modules_discover(&card,identity,FX_SDK_CAPS,scratch,sizeof(scratch),&found,memory)==FX_IO && !found.entries);
    fail_read=0u;fail_close=1u;
    assert(fx_modules_discover(&card,identity,FX_SDK_CAPS,scratch,sizeof(scratch),&found,memory)==FX_CLOSE && !found.entries);
    fx_module_manager_shutdown(&manager);fx_module_index_destroy(&manager.index);
    for(uint32_t i=0u;i<3u;++i)free(files[i].data);
    puts("{\"inventory\":true,\"ui_reasons\":true,\"card_errors_preserved\":true}");return 0;
}
int main(int argc,char **argv) {
    memset(identity,0x11,sizeof(identity));memcpy(platform.identity,identity,32u);memcpy(platform.module_identity,identity,32u);
    assert(argc>=3);
    if(!strcmp(argv[1],"inventory"))return inventory(argc,argv);
    const char *mode=argv[2];
    if(!strcmp(mode,"missing"))platform.count=0u;
    if(!strcmp(mode,"version"))bindings[0].version=2u;
    if(!strcmp(mode,"kind"))bindings[0].kind=3u;
    if(!strcmp(mode,"abi"))bindings[0].abi=2u;
    if(!strcmp(mode,"unavailable"))platform.io.resource=0;
    if(!strcmp(mode,"service"))environment.service_abi=2u;
    static const uint8_t foreign_identity[32]={0};
    if(!strcmp(mode,"foreign-environment"))environment.identity=foreign_identity;
    if(!strcmp(mode,"many"))many=1u;
    uint32_t bytes;uint8_t *package=read_file(argv[1],&bytes),digest[32];fx_sha256(package,bytes,digest);
    static _Alignas(4096) uint8_t image[8192];memset(image,0xa5,sizeof(image));
    FxModule module;memset(&module,0x5a,sizeof(module));FxModuleIssue issue;
    uint64_t caps=!strcmp(mode,"limited")?FX_SDK_QUERY:FX_SDK_CAPS;
    const FxModuleEnvironment *env=!strcmp(mode,"no-environment")?0:&environment;
    FxResult r=fx_module_prepare_environment(package,bytes,digest,identity,caps,image,sizeof(image),env,&module,&issue);
    if(r!=FX_OK) {
        for(uint32_t i=0u;i<sizeof(image);++i)assert(image[i]==0xa5u);
        assert(!module.prepared && !module.bytes && !module.segment_count);
    } else {assert(module.prepared && module.bytes==4096u && image[0]==0xc0u && image[4]==0u);}
    assert(!native_calls);report(&issue,r);free(package);return 0;
}
