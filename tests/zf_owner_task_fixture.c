#include <assert.h>
#include <string.h>
#include "owner_storage.h"
static FxZfNativeTaskRequest request;
static FxZfOwnerStorage *current;
static int32_t create(const FxZfNativeTaskRequest *r){request=*r;return 9;}
static int32_t start(int32_t id){assert(id==9);return 0;}
static int32_t delay(uint32_t ticks) {
    assert(ticks==100u);
    if(atomic_load(&current->worker.steps)==1u)fx_zf_lifecycle_record(&current->lifecycle,25u);
    else fx_zf_lifecycle_record(&current->lifecycle,8u);
    return 0;
}
FxZfLifecycle *fx_zf_runtime_lifecycle(void){return 0;}
uint32_t fx_zf_ptp_poll(void){return 0u;}
FxNativeModules *fx_zf_owner_modules_manager(void){return 0;}
void fx_module_manager_tick(FxModuleManager *manager){(void)manager;}
int main(void) {
    FxZfOwnerStorage owner={0};current=&owner;
    fx_zf_lifecycle_record(&owner.lifecycle,20u);
    const FxZfNativeTaskOps ops={create,start};
    assert(fx_zf_owner_task_start(&owner,&ops,delay,100u,27u,1u)==FX_OK);
    assert(owner.worker.scope.phase==FX_ZF_TASK_START_ACCEPTED);
    assert(!atomic_load(&owner.worker.entered));
    assert(request.argument==(uintptr_t)&owner && request.stack==(uintptr_t)owner.worker.stack);
    assert(request.stack_bytes==FX_MODULE_STACK_DEFAULT && request.priority==27u && request.native_mode==1u);
    ((void (*)(void *))request.entry)((void *)request.argument);
    assert(atomic_load(&owner.worker.entered) && atomic_load(&owner.worker.finished));
    assert(atomic_load(&owner.worker.steps)==3u && atomic_load(&owner.worker.notices)==3u);
    assert(atomic_load(&owner.worker.last_word)&FX_ZF_LIFE_STOP);
    assert(!fx_zf_lifecycle_accepts_work(&owner.lifecycle));
    assert(fx_zf_owner_task_start(&owner,&ops,delay,100u,27u,1u)==FX_BUSY);
    return 0;
}
