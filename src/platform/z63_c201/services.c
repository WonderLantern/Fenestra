#include "services.h"
#include "fenestra_platform_native.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_tasks.h"
#include "fenestra_scene.h"
#include "fenestra_native_scene.h"
const uint32_t fx_z63_scene_layout[]={0x46585336u,1u,offsetof(FxNativeSceneState,display),
    offsetof(FxNativeDisplay,object),offsetof(FxNativeDisplay,surface),offsetof(FxNativeDisplay,width),
    offsetof(FxNativeDisplay,height),offsetof(FxNativeDisplay,callbacks),offsetof(FxNativeDisplay,target),
    offsetof(FxNativeDisplay,viewport),offsetof(FxNativeDisplay,frames),offsetof(FxNativeDisplay,error)};
extern void fx_z63_probe_module_result(uint32_t,uint32_t,uint64_t);
#include "fenestra_accounting_service.h"
static void *allocate(void *context,uint32_t n){(void)context;return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u)(n,0u);}
static void release(void *context,void *p){(void)context;((void (*)(void *))(uintptr_t)0x40d32d40u)(p);}
FxResult fx_z63_services_init(FxZ63Services *s, uint32_t owner, uint32_t generation, uint32_t caps) {
    if (!s)
        return FX_ARGUMENT;
    FxResult result=fx_runtime_accounts_init(&s->accounting,owner,generation,(FxIndexMemory){0,allocate,release});
    if(result==FX_OK)result=fx_platform_init(&s->platform,fx_platform_target(),owner,generation,caps);
    if(result==FX_OK){s->platform.accounting=&s->accounting.state;fx_account_owner_bind(&s->platform);}
    return result;
}
FxResult fx_z63_service_call(void *context, const FxServiceRequest *q, FxServiceReply *a) {
    FxZ63Services *s = context;
    if (!s)
        return FX_ARGUMENT;
#ifdef FX_Z63_UI
    uint64_t before=fx_platform_ui_stamp(&s->platform);
#endif
    FxResult result=fx_platform_dispatch(&s->platform, q, a);
    if(result==FX_OK && q->operation==FX_S_PLATFORM && q->arg[0]==FX_BIND_CALL &&
       q->arg[2]==FX_SCENE_SUBMIT && q->bytes==sizeof(FxSceneSubmission)) {
        const FxSceneSubmission *scene=(const void *)q->data;
        if(scene->abi==FX_SCENE_ABI && scene->page==FX_SCENE_GLOBAL_PAGE)
            fx_z63_probe_module_result(75u,0u,a->value[0]);
    }
#ifdef FX_Z63_UI
    if(fx_platform_ui_changed(&s->platform,before))fx_native_ui_refresh_owner(&s->platform);
#endif
    if(result==FX_OK && q->operation==FX_S_PLATFORM && q->arg[0]==FX_BIND_CALL &&
       q->arg[2]==FX_TASK_STATUS && a->bytes==sizeof(FxTaskStatus)) {
        const FxTaskStatus *status=(const void *)a->data;
        fx_z63_probe_module_result(53u,status->state,status->value[0]);
        fx_z63_probe_module_result(54u,status->result,status->task);
    }
    return result;
}
FxResult fx_z63_service_stop(void *context, uint32_t panic) {
    FxZ63Services *s = context;
    if (!s)
        return FX_ARGUMENT;
    return fx_platform_stop(&s->platform, panic);
}
