/* Real shared platform/resource/hook code; all native IO is synthetic. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "fenestra_hooks.h"
#include "fenestra_platform_native.h"
#include "fenestra_sdk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FxResourceRegistry registry;
static FxHookRegistry hooks;
static uint32_t attaches,detaches,calls,writes,attach_fail,detach_busy,detach_error,lock_after_detach;
static _Atomic uintptr_t native_slot;
static void *allocate(uint32_t bytes,uint32_t flags){(void)flags;return calloc(1u,bytes);}
static void release(void *p){free(p);}
FxHookRegistry *fx_native_hooks(void){return &hooks;}
FxResult fx_native_hook_page(FxPlatformOwner *o,uint8_t *p,FxHookCell *c,uint32_t *retained) {
    (void)o;(void)p;(void)c;*retained=0u;return FX_CAPABILITY;
}
void fx_native_module_fault(FxPlatformOwner *o,FxResult r){(void)o;(void)r;abort();}
static FxNamedBinding bindings[]={
    {"test.function",FX_BIND_FUNCTION,FX_CALL_U64_8,1u,0u,0u,0u,0x1000u,{0}},
    {"test.fn.alias",FX_BIND_FUNCTION,FX_CALL_U64_8,1u,0u,0u,0u,0x1000u,{0}},
    {"test.table",FX_BIND_TABLE,FX_CALL_BYTES,1u,FX_BIND_READ|FX_BIND_WRITE,0u,64u,0x4000u,{0}},
    {"test.table.alias",FX_BIND_TABLE,FX_CALL_BYTES,1u,FX_BIND_READ,0u,64u,0x4020u,{0}},
    {"test.read",FX_BIND_TABLE,FX_CALL_BYTES,1u,FX_BIND_READ,0u,64u,0x4000u,{0}},
    {"test.other",FX_BIND_TABLE,FX_CALL_BYTES,1u,FX_BIND_READ|FX_BIND_WRITE,0u,32u,0x5000u,{0}},
    {"test.device",FX_BIND_DEVICE,FX_CALL_DEVICE,1u,0u,0u,0u,1u,{0}},
    {"test.device2",FX_BIND_DEVICE,FX_CALL_DEVICE,1u,0u,0u,0u,1u,{0}},
    {"test.exclusive",FX_BIND_DEVICE,FX_CALL_DEVICE,1u,FX_BIND_EXCLUSIVE,0u,0u,1u,{0}},
    {"core.hooks",FX_BIND_DEVICE,FX_CALL_DEVICE,1u,0u,0u,0u,1u,{0}},
    {"hook.slot",FX_BIND_TABLE,FX_CALL_BYTES,1u,FX_BIND_READ|FX_BIND_WRITE,0u,8u,0u,{0}},
    {"rtos.memory.allocate",FX_BIND_FUNCTION,FX_CALL_U64_8,1u,0u,0u,0u,0u,{0}},
    {"rtos.memory.release",FX_BIND_FUNCTION,FX_CALL_U64_8,1u,0u,0u,0u,0u,{0}}
};
static FxResult native_read(void *c,uint64_t at,void *out,uint32_t n){(void)c;(void)at;memset(out,0x55,n);return FX_OK;}
static FxResult native_write(void *c,uint64_t at,const void *p,uint32_t n){(void)c;(void)at;(void)p;(void)n;++writes;return FX_OK;}
static FxResult native_call(void *c,const FxNamedBinding *b,const uint64_t args[8],uint64_t *out) {
    (void)c;(void)b;(void)args;++calls;*out=123u;return FX_OK;
}
static FxResult resource(void *c,uint32_t op,const FxNamedBinding *b,FxPlatformOwner *o,uint64_t token,uint64_t *cookie,
    const FxServiceRequest *q,FxServiceReply *a) {
    if(!strcmp(b->name,"core.hooks"))return fx_hook_resource(c,op,b,o,token,cookie,q,a);
    if(op==FX_RESOURCE_ATTACH){++attaches;*cookie=attach_fail==1u?0u:42u;return attach_fail?FX_IO:FX_OK;}
    if(op==FX_RESOURCE_DETACH){++detaches;if(detach_busy)return FX_BUSY;
        if(detach_error){if(lock_after_detach)atomic_store(&registry.lock,1u);return FX_IO;}*cookie=0u;
        if(lock_after_detach)atomic_store(&registry.lock,1u);return FX_OK;}
    if(op==FX_RESOURCE_CALL){++calls;return FX_OK;}
    return FX_OK;
}
static FxPlatform platform={.abi=1u,.count=sizeof(bindings)/sizeof(*bindings),.model="synthetic",.firmware="conflicts",
    .bindings=bindings,.io={.read=native_read,.write=native_write,.call=native_call,.resource=resource}};
static FxPlatformOwner *owner(uint32_t id) {
    FxPlatformOwner *p=calloc(1u,sizeof(*p));assert(p);
    assert(fx_platform_init(p,&platform,id,1u,FX_SDK_CAPS)==FX_OK);p->conflicts=&registry;return p;
}
static FxServiceReply reply;
static FxResult dispatch(FxPlatformOwner *o,uint32_t op,uint64_t handle,uint64_t sub,uint64_t length,const void *data,uint32_t bytes) {
    FxServiceRequest q={.operation=FX_S_PLATFORM,.arg={op,handle,sub,length},.bytes=bytes};
    assert(bytes<=sizeof(q.data));if(bytes)memcpy(q.data,data,bytes);memset(&reply,0,sizeof(reply));
    return fx_platform_dispatch(o,&q,&reply);
}
static FxResult open_binding(FxPlatformOwner *o,const char *name,uint32_t mode) {
    char copied[64]={0};assert(strlen(name)<64u);memcpy(copied,name,strlen(name));
    return dispatch(o,FX_BIND_OPEN,1u,0u,mode,copied,sizeof(copied));
}
static uint64_t opened(FxPlatformOwner *o,const char *name,uint32_t mode) {
    assert(open_binding(o,name,mode)==FX_OK);assert(reply.value[0]);return reply.value[0];
}
static void close_binding(FxPlatformOwner *o,uint64_t token){assert(dispatch(o,FX_BIND_CLOSE,token,0u,0u,0,0u)==FX_OK);}
static FxResult reserve(FxPlatformOwner *o,const char *name,uint32_t mode) {
    char copied[64]={0};assert(strlen(name)<64u);memcpy(copied,name,strlen(name));
    return dispatch(o,FX_BIND_RESERVE,0u,mode,0u,copied,sizeof(copied));
}
static void conflict(FxPlatformOwner *requester,uint32_t holder,const char *name) {
    assert(reply.bytes==sizeof(FxResourceConflict));FxResourceConflict copied;memcpy(&copied,reply.data,sizeof(copied));
    assert(copied.abi==1u && copied.holder==holder && copied.requester==requester->owner && !strcmp(copied.requested,name));
    assert(dispatch(requester,FX_BIND_CONFLICT,0u,0u,0u,0,0u)==FX_OK);
    assert(!memcmp(reply.data,&copied,sizeof(copied)));
}
static void functions(void) {
    FxPlatformOwner *a=owner(1u),*b=owner(2u);
    uint64_t x=opened(a,"test.function",0u),y=opened(b,"test.fn.alias",0u),args[8]={0};
    assert(dispatch(a,FX_BIND_CALL,x,0u,0u,args,sizeof(args))==FX_OK);
    assert(dispatch(b,FX_BIND_CALL,y,0u,0u,args,sizeof(args))==FX_OK && calls==2u);
    close_binding(a,x);close_binding(b,y);assert(!registry.count && !registry.head);
    x=opened(a,"test.function",0u);
    uint64_t previous_token=b->next_token;
    for(uint32_t i=0u;i<100u;++i)assert(open_binding(b,"test.fn.alias",FX_RESOURCE_EXCLUSIVE)==FX_CONFLICT);
    assert(!b->binding_count && b->next_token==previous_token && calls==2u);conflict(b,1u,"test.fn.alias");
    close_binding(a,x);y=opened(b,"test.fn.alias",FX_RESOURCE_EXCLUSIVE);
    assert(open_binding(a,"test.function",0u)==FX_CONFLICT);close_binding(b,y);
}
static void tables(void) {
    FxPlatformOwner *a=owner(1u),*b=owner(2u),*c=owner(3u);
    uint64_t x=opened(a,"test.table",0u);
    assert(open_binding(b,"test.table.alias",0u)==FX_CONFLICT);conflict(b,1u,"test.table.alias");
    uint64_t other=opened(b,"test.other",0u);close_binding(b,other);close_binding(a,x);
    x=opened(a,"test.read",0u);uint64_t y=opened(b,"test.read",0u);
    assert(open_binding(c,"test.table",0u)==FX_CONFLICT);
    uint64_t shared=opened(a,"test.table",FX_RESOURCE_SHARED);uint8_t data=7u;
    assert(dispatch(a,FX_BIND_STORE,shared,0u,1u,&data,1u)==FX_CAPABILITY && !writes);
    assert(dispatch(a,FX_BIND_COPY,shared,0u,1u,0,0u)==FX_OK && reply.bytes==1u);
    close_binding(a,shared);close_binding(a,x);close_binding(b,y);assert(!registry.count);
}
static void groups(void) {
    FxPlatformOwner *a=owner(1u),*b=owner(2u);
    uint64_t x=opened(a,"test.device",0u),y=opened(b,"test.device",0u);
    assert(open_binding(owner(3u),"test.device",FX_RESOURCE_EXCLUSIVE)==FX_CONFLICT && attaches==2u);
    uint64_t other=opened(b,"test.device2",FX_RESOURCE_EXCLUSIVE);close_binding(b,other);
    close_binding(a,x);close_binding(b,y);
    x=opened(a,"test.exclusive",0u);
    assert(open_binding(b,"test.exclusive",0u)==FX_CONFLICT);close_binding(a,x);
    assert(open_binding(b,"test.exclusive",FX_RESOURCE_SHARED)==FX_CAPABILITY);
    uint32_t native_count=attaches;
    assert(reserve(a,"nikon.sensor.pipeline",FX_RESOURCE_EXCLUSIVE)==FX_OK);x=reply.value[0];
    assert(reserve(b,"nikon.sensor.pipeline",FX_RESOURCE_SHARED)==FX_CONFLICT);conflict(b,1u,"nikon.sensor.pipeline");
    assert(reserve(b,"nikon.encoder.pipeline",FX_RESOURCE_EXCLUSIVE)==FX_OK);y=reply.value[0];
    assert(attaches==native_count);close_binding(a,x);close_binding(b,y);assert(!registry.count);
}
static void failed_attach(void) {
    FxPlatformOwner *a=owner(1u),*b=owner(2u);attach_fail=1u;
    assert(open_binding(a,"test.device",FX_RESOURCE_EXCLUSIVE)==FX_IO && !registry.count && !a->binding_count);
    attach_fail=0u;uint64_t y=opened(b,"test.device",FX_RESOURCE_EXCLUSIVE);close_binding(b,y);
    attach_fail=2u;assert(open_binding(a,"test.device",FX_RESOURCE_EXCLUSIVE)==FX_IO);uint64_t x=reply.value[0];
    assert(x && registry.count==1u);attach_fail=0u;
    assert(open_binding(b,"test.device",0u)==FX_CONFLICT);
    detach_busy=1u;assert(dispatch(a,FX_BIND_CLOSE,x,0u,0u,0,0u)==FX_BUSY);
    assert(open_binding(b,"test.device",0u)==FX_CONFLICT);
    detach_busy=0u;close_binding(a,x);y=opened(b,"test.device",FX_RESOURCE_EXCLUSIVE);close_binding(b,y);
}
static void detach_retry(void) {
    FxPlatformOwner *a=owner(1u),*b=owner(2u);uint64_t x=opened(a,"test.device",FX_RESOURCE_EXCLUSIVE);
    lock_after_detach=1u;assert(dispatch(a,FX_BIND_CLOSE,x,0u,0u,0,0u)==FX_BUSY && detaches==1u);
    assert(open_binding(b,"test.device",0u)==FX_BUSY && attaches==1u);
    atomic_store(&registry.lock,0u);lock_after_detach=0u;
    assert(open_binding(b,"test.device",0u)==FX_CONFLICT);
    close_binding(a,x);assert(detaches==1u);uint64_t y=opened(b,"test.device",0u);close_binding(b,y);
}
static void shutdown(void) {
    FxPlatformOwner *a=owner(1u),*b=owner(2u);
    (void)opened(a,"test.function",FX_RESOURCE_EXCLUSIVE);
    assert(fx_platform_stop(a,0u)==FX_OK && !registry.count && a->phase==FX_OWNER_QUIESCED);
    (void)opened(b,"test.function",FX_RESOURCE_EXCLUSIVE);
    assert(fx_platform_stop(b,1u)==FX_OK && registry.count==1u);
    assert(open_binding(owner(3u),"test.function",0u)==FX_CONFLICT);
    assert(fx_platform_stop(b,0u)==FX_STATE && registry.count==1u);
}
static void stop_error(void) {
    FxPlatformOwner *a=owner(1u);
    (void)opened(a,"test.device",FX_RESOURCE_EXCLUSIVE);(void)opened(a,"test.function",0u);
    detach_error=lock_after_detach=1u;
    assert(fx_platform_stop(a,0u)==FX_IO && registry.count==2u && detaches==1u);
    atomic_store(&registry.lock,0u);
}
static void registry_edges(void) {
    FxResourceClaim claims[3]={0};FxResourceConflict c;
    atomic_store(&registry.lock,1u);
    assert(fx_resource_acquire(&registry,&claims[0],1u,1u,"one",FX_RESOURCE_MEMORY,100u,8u,1u,&c)==FX_BUSY && !claims[0].registry);
    atomic_store(&registry.lock,0u);
    assert(fx_resource_acquire(&registry,&claims[0],1u,1u,"one",FX_RESOURCE_MEMORY,100u,8u,1u,&c)==FX_OK);
    assert(fx_resource_acquire(&registry,&claims[1],1u,1u,"alias",FX_RESOURCE_MEMORY,104u,8u,2u,&c)==FX_OK);
    assert(fx_resource_acquire(&registry,&claims[2],1u,2u,"new generation",FX_RESOURCE_MEMORY,104u,4u,2u,&c)==FX_CONFLICT);
    assert(fx_resource_acquire(&registry,&claims[2],2u,1u,"overflow",FX_RESOURCE_MEMORY,UINT64_MAX-1u,8u,1u,&c)==FX_ARGUMENT);
    atomic_store(&registry.lock,1u);assert(fx_resource_release_many(claims,2u)==FX_BUSY && registry.count==2u);
    atomic_store(&registry.lock,0u);assert(fx_resource_release_many(claims,2u)==FX_OK && !registry.count);
    assert(fx_resource_acquire(&registry,&claims[0],1u,1u,"adjacent",FX_RESOURCE_MEMORY,100u,8u,2u,&c)==FX_OK);
    assert(fx_resource_acquire(&registry,&claims[1],2u,1u,"adjacent2",FX_RESOURCE_MEMORY,108u,8u,2u,&c)==FX_OK);
    assert(fx_resource_release_many(claims,2u)==FX_OK);
}
static FxResult service_call(void *context,const FxServiceRequest *q,FxServiceReply *a) {
    return fx_platform_dispatch(context,q,a);
}
static void copied_sdk(void) {
    FxPlatformOwner *a=owner(1u),*b=owner(2u);uint64_t held=opened(a,"test.device",FX_RESOURCE_EXCLUSIVE);
    FxServiceOwner service;fx_service_init(&service,2u,1u,FX_SDK_CAPS,(FxServiceBackend){b,service_call,0});
    FxServiceExchange exchange={0};FxServiceModule api={.service_abi=1u,.owner=2u,.generation=1u,.exchange=&exchange};
    FxSdkClient client={0};FxServiceReply taken;
    assert(fx_sdk_open_access(&client,&api,"test.device",1u,FX_RESOURCE_SHARED)==FX_OK);
    assert(fx_service_dispatch(&service,&exchange.request,&exchange.reply)==FX_CONFLICT && !service.fault && service.active);
    assert(fx_sdk_take(&client,&api,&taken)==FX_OK && taken.result==FX_CONFLICT && taken.bytes==sizeof(FxResourceConflict));
    assert(fx_sdk_resource_conflict(&client,&api)==FX_OK);
    assert(fx_service_dispatch(&service,&exchange.request,&exchange.reply)==FX_OK);
    assert(fx_sdk_take(&client,&api,&taken)==FX_OK && taken.result==FX_OK);
    assert(fx_sdk_reserve(&client,&api,"org.example.sensor",FX_RESOURCE_EXCLUSIVE)==FX_OK);
    assert(fx_service_dispatch(&service,&exchange.request,&exchange.reply)==FX_OK);
    assert(fx_sdk_take(&client,&api,&taken)==FX_OK && taken.result==FX_OK);
    close_binding(b,taken.value[0]);close_binding(a,held);assert(!registry.count && !service.fault);
    b->conflicts=0;assert(open_binding(b,"test.device",FX_RESOURCE_EXCLUSIVE)==FX_CAPABILITY);
    assert(reserve(b,"org.example.sensor",FX_RESOURCE_EXCLUSIVE)==FX_CAPABILITY);
}
static void scale(void) {
    FxPlatformOwner *owners[200];
    for(uint32_t i=0u;i<200u;++i){owners[i]=owner(i+1u);(void)opened(owners[i],"test.function",0u);}
    assert(registry.count==200u);
    FxPlatformOwner *more=owner(201u);assert(open_binding(more,"test.fn.alias",2u)==FX_CONFLICT);
    for(uint32_t i=0u;i<200u;++i)assert(fx_platform_stop(owners[i],0u)==FX_OK);
    char name[64]={0};memcpy(name,"test.function",14u);
    for(uint32_t i=0u;i<300u;++i)assert(dispatch(more,FX_BIND_OPEN,1u,1u,0u,name,sizeof(name))==FX_OK);
    assert(registry.count==300u && more->bindings.chunks>0u);
    assert(fx_platform_stop(more,0u)==FX_OK && !registry.count && !registry.head);
}
static FxResult upload_hook(FxPlatformOwner *o,uint64_t token,FxHookProgram *p) {
    assert(dispatch(o,FX_BIND_CALL,token,FX_INTERCEPT_BEGIN,sizeof(*p),0,0u)==FX_OK);
    for(uint32_t offset=0u;offset<sizeof(*p);) {
        uint32_t bytes=(uint32_t)sizeof(*p)-offset;if(bytes>FX_SERVICE_BYTES)bytes=FX_SERVICE_BYTES;
        assert(dispatch(o,FX_BIND_CALL,token,FX_INTERCEPT_APPEND,offset,(uint8_t *)p+offset,bytes)==FX_OK);offset+=bytes;
    }
    return dispatch(o,FX_BIND_CALL,token,FX_INTERCEPT_COMMIT,0u,0,0u);
}
static void hook_lifetime(uint32_t shared) {
    static FxHookPage page;static uint8_t code[FX_HOOK_PAGE_BYTES];
    page.ready=1u;page.code=code;atomic_store(&hooks.pages,&page);atomic_store(&native_slot,0x7000u);
    FxPlatformOwner *a=owner(1u),*b=owner(2u);
    uint64_t table=opened(a,"hook.slot",shared?FX_RESOURCE_SHARED:0u),hook=opened(a,"core.hooks",0u);
    uint64_t function=opened(a,"test.function",0u);
    FxHookProgram p={.abi=1u,.bytes=sizeof(p),.table=table,.expected=0x7000u,.before_count=1u,
        .before={{.op=FX_VM_CALL,.value=function}}};
    FxResult r=upload_hook(a,hook,&p);
    if(shared){assert(r==FX_CAPABILITY && atomic_load(&native_slot)==0x7000u);close_binding(a,hook);
        close_binding(a,table);close_binding(a,function);assert(!registry.count);return;}
    assert(r==FX_OK && atomic_load(&native_slot)!=0x7000u);
    close_binding(a,table);close_binding(a,function);
    assert(open_binding(b,"hook.slot",0u)==FX_CONFLICT);
    assert(open_binding(b,"test.fn.alias",FX_RESOURCE_EXCLUSIVE)==FX_CONFLICT);
    atomic_store(&registry.lock,1u);
    assert(dispatch(a,FX_BIND_CLOSE,hook,0u,0u,0,0u)==FX_BUSY && atomic_load(&native_slot)==0x7000u);
    atomic_store(&registry.lock,0u);
    assert(open_binding(b,"hook.slot",0u)==FX_CONFLICT);
    close_binding(a,hook);assert(!registry.count);
    table=opened(b,"hook.slot",0u);function=opened(b,"test.fn.alias",FX_RESOURCE_EXCLUSIVE);
    close_binding(b,table);close_binding(b,function);assert(!registry.count);
}
int main(int argc,char **argv) {
    assert(argc==2);bindings[10].address=(uintptr_t)&native_slot;
    bindings[11].address=(uintptr_t)allocate;bindings[12].address=(uintptr_t)release;
    if(!strcmp(argv[1],"functions"))functions();else if(!strcmp(argv[1],"tables"))tables();
    else if(!strcmp(argv[1],"groups"))groups();else if(!strcmp(argv[1],"failed_attach"))failed_attach();
    else if(!strcmp(argv[1],"detach_retry"))detach_retry();else if(!strcmp(argv[1],"shutdown"))shutdown();
    else if(!strcmp(argv[1],"registry_edges"))registry_edges();else if(!strcmp(argv[1],"scale"))scale();
    else if(!strcmp(argv[1],"stop_error"))stop_error();
    else if(!strcmp(argv[1],"copied_sdk"))copied_sdk();
    else if(!strcmp(argv[1],"hook_lifetime"))hook_lifetime(0u);else if(!strcmp(argv[1],"hook_shared"))hook_lifetime(1u);
    else abort();
    printf("{\"passed\":true,\"case\":\"%s\",\"native_camera_used\":false}\n",argv[1]);return 0;
}
