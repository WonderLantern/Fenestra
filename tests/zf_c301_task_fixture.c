/* Host-only stubs: this does not execute native firmware or a scheduler. */
#include "task.h"
#include <stdint.h>
#include <string.h>

static FxZfNativeTaskRequest seen;
static int creates,starts;
static int32_t create_result,start_result;
static FxZfTaskScope *active_scope;
static FxZfNativeTaskOps *mutable_ops;
static int redirected;
static _Alignas(16) uint8_t shared[4096];
static int32_t create_task(const FxZfNativeTaskRequest *r) __attribute__((aligned(4)));
static int32_t create_task(const FxZfNativeTaskRequest *r) {
    seen=*r;
    ++creates;
    return create_result;
}
static int32_t start_task(int32_t id) __attribute__((aligned(4)));
static int32_t start_task(int32_t id) {
    if(!active_scope || active_scope->phase!=FX_ZF_TASK_CREATED || active_scope->id!=id)return -999;
    ++starts;
    return start_result;
}
static int32_t wrong_start(int32_t id) __attribute__((aligned(4)));
static int32_t wrong_start(int32_t id) {(void)id;++redirected;return -41;}
static int32_t create_and_change_ops(const FxZfNativeTaskRequest *r) __attribute__((aligned(4)));
static int32_t create_and_change_ops(const FxZfNativeTaskRequest *r) {
    seen=*r;++creates;mutable_ops->start=wrong_start;return 42;
}
static void entry(void *arg) __attribute__((aligned(4)));
static void entry(void *arg) {(void)arg;}

int main(void) {
    _Alignas(16) uint8_t stack[4096];
    FxZfNativeTaskOps ops={create_task,start_task};
    FxZfTaskConfig config={entry,(void *)(uintptr_t)0x1000u,stack,sizeof(stack),4u,2u};
    FxZfTaskScope scope={0};
    memset(stack,0x5a,sizeof(stack));
    active_scope=&scope;
    create_result=42;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_OK || creates!=1 || starts!=1)return 1;
    if(scope.phase!=FX_ZF_TASK_START_ACCEPTED || scope.id!=42 || scope.start_result!=0 ||
       scope.stack!=(uintptr_t)stack || scope.stack_bytes!=sizeof(stack))return 2;
    if(seen.attributes || seen.reserved || seen.reserved2 || seen.argument!=(uintptr_t)config.argument ||
       seen.entry!=(uintptr_t)entry || seen.priority!=4 || seen.stack!=(uintptr_t)stack ||
       seen.stack_bytes!=sizeof(stack) || seen.native_mode!=2 || seen.affinity!=UINT32_MAX)return 3;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_BUSY || creates!=1 || starts!=1)return 4;

    memset(&scope,0,sizeof(scope));creates=starts=0;create_result=-17;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_IO || scope.phase!=FX_ZF_TASK_CREATE_FAILED ||
       scope.id!=-17 || scope.stack!=(uintptr_t)stack || scope.stack_bytes!=sizeof(stack) || creates!=1 || starts)return 5;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_BUSY || creates!=1)return 6;

    memset(&scope,0,sizeof(scope));creates=starts=0;create_result=73;start_result=-41;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_IO || scope.phase!=FX_ZF_TASK_START_FAILED ||
       scope.id!=73 || scope.start_result!=-41 || scope.stack!=(uintptr_t)stack || creates!=1 || starts!=1)return 7;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_BUSY || creates!=1 || starts!=1)return 8;

    memset(&scope,0,sizeof(scope));creates=starts=0;config.priority=0;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || scope.phase || scope.stack || creates || starts)return 9;
    config.priority=257;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 10;
    config.priority=4;config.native_mode=0;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 11;
    config.native_mode=4;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 12;
    config.native_mode=2;config.stack=(void *)((uintptr_t)stack+1u);
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 13;
    config.stack=stack;config.stack_bytes=4095;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 14;
    config.stack=(void *)(uintptr_t)(UINTPTR_MAX-15u);config.stack_bytes=4096;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 15;
    config.stack=stack;config.stack_bytes=sizeof(stack);config.priority=1;config.native_mode=1;
    create_result=1;start_result=0;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_OK || seen.priority!=1 || seen.native_mode!=1)return 16;
    memset(&scope,0,sizeof(scope));creates=starts=0;config.priority=256;config.native_mode=3;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_OK || seen.priority!=256 || seen.native_mode!=3)return 17;
    memset(&scope,0,sizeof(scope));creates=starts=0;create_result=0;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_IO || scope.id!=0 ||
       scope.phase!=FX_ZF_TASK_CREATE_FAILED || scope.stack!=(uintptr_t)stack || starts)return 18;
    memset(&scope,0,sizeof(scope));creates=starts=0;ops.start=0;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || scope.phase || scope.stack || creates)return 19;
    for(size_t i=0;i<sizeof(stack);i++)if(stack[i]!=0x5a)return 20;

    ops.create=create_task;ops.start=start_task;config.priority=4;config.native_mode=2;
    memset(shared,0,sizeof(shared));creates=starts=0;config.stack=shared;
    FxZfTaskScope *shared_scope=(void *)shared;
    if(fx_zf_task_start(shared_scope,&config,&ops)!=FX_ARGUMENT || creates || starts || shared_scope->phase)return 21;
    memset(shared,0,sizeof(shared));memset(&scope,0,sizeof(scope));
    FxZfTaskConfig *shared_config=(void *)shared;
    *shared_config=(FxZfTaskConfig){entry,0,shared,sizeof(shared),4u,2u};
    if(fx_zf_task_start(&scope,shared_config,&ops)!=FX_ARGUMENT || creates || starts || scope.phase)return 22;
    memset(shared,0,sizeof(shared));
    FxZfNativeTaskOps *shared_ops=(void *)shared;
    *shared_ops=ops;
    if(fx_zf_task_start(&scope,&config,shared_ops)!=FX_ARGUMENT || creates || starts || scope.phase)return 23;
    memset(shared,0,sizeof(shared));
    if(fx_zf_task_start((void *)shared,(void *)(shared+8),&ops)!=FX_ARGUMENT || creates || starts)return 24;
    if(fx_zf_task_start((void *)shared,&config,(void *)(shared+8))!=FX_ARGUMENT || creates || starts)return 25;
    if(fx_zf_task_start(&scope,(void *)shared,(void *)(shared+8))!=FX_ARGUMENT || creates || starts)return 26;

    config.stack=stack;config.entry=(void (*)(void *))(uintptr_t)(stack+16);config.argument=0;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 27;
    config.entry=entry;config.argument=stack+16;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 28;
    config.argument=0;config.entry=entry;
    ops.create=(int32_t (*)(const FxZfNativeTaskRequest *))(uintptr_t)(stack+32);
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 29;
    ops.create=create_task;
    ops.start=(int32_t (*)(int32_t))(uintptr_t)(stack+64);
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_ARGUMENT || creates || starts)return 30;
    ops.start=start_task;
    config.argument=0;ops.create=create_and_change_ops;ops.start=start_task;mutable_ops=&ops;
    active_scope=&scope;creates=starts=redirected=0;start_result=0;
    if(fx_zf_task_start(&scope,&config,&ops)!=FX_OK || creates!=1 || starts!=1 || redirected ||
       ops.start!=wrong_start || scope.phase!=FX_ZF_TASK_START_ACCEPTED)return 31;
    memset(&scope,0,sizeof(scope));creates=starts=0;ops.create=create_task;ops.start=start_task;
    if(fx_zf_task_start((void *)(shared+1),&config,&ops)!=FX_ARGUMENT || creates || starts)return 36;
    if(fx_zf_task_start(&scope,(void *)(shared+1),&ops)!=FX_ARGUMENT || creates || starts)return 32;
    if(fx_zf_task_start(&scope,&config,(void *)(shared+1))!=FX_ARGUMENT || creates || starts)return 33;
    FxZfNativeTaskOps invalid=ops;
    invalid.create=(int32_t (*)(const FxZfNativeTaskRequest *))(uintptr_t)1u;
    if(fx_zf_task_start(&scope,&config,&invalid)!=FX_ARGUMENT || creates || starts)return 34;
    invalid=ops;invalid.start=(int32_t (*)(int32_t))(uintptr_t)2u;
    if(fx_zf_task_start(&scope,&config,&invalid)!=FX_ARGUMENT || creates || starts)return 35;
    return 0;
}
