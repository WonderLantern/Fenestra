/* Run the unchanged demo against Zf's real protocol/platform backend.
 * Native graphics are represented by a copied scene sink, not camera calls. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "services.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_scene.h"
#include "fenestra_accounting_service.h"

extern int fenestra_module_entry(const FxModuleServices *);
static FxNativeUiDispatch dispatch;
static uint32_t frames,refreshes,attach_calls,submit_attempts;
static const FxNamedBinding bindings[]={
    {.name="camera.display.scene",.kind=FX_BIND_DEVICE,.abi=FX_CALL_DEVICE,
     .version=1u,.capabilities=FX_SDK_BINDING,.address=1u}
};
static FxResult resource(void *context,uint32_t operation,const FxNamedBinding *binding,
    FxPlatformOwner *owner,uint64_t token,uint64_t *cookie,
    const FxServiceRequest *request,FxServiceReply *reply) {
    (void)context;(void)owner;(void)token;
    assert(binding==bindings);
    if(operation==FX_RESOURCE_ATTACH){++attach_calls;*cookie=1u;return FX_OK;}
    if(operation==FX_RESOURCE_DETACH || operation==FX_RESOURCE_PANIC){*cookie=0u;return FX_OK;}
    assert(operation==FX_RESOURCE_CALL && request && reply);
    if(request->arg[2]==FX_SCENE_QUERY) {
        FxSceneInfo info={FX_SCENE_ABI,sizeof(info),FX_SCENE_COMMANDS,FX_SCENE_EXTENT,
            0u,1u,1u,FX_SCENE_CAP_PAGE};
        memcpy(reply->data,&info,sizeof(info));reply->bytes=sizeof(info);return FX_OK;
    }
    assert(request->arg[2]==FX_SCENE_SUBMIT && request->bytes==sizeof(FxSceneSubmission));
    FxSceneSubmission scene;memcpy(&scene,request->data,sizeof(scene));
    assert(scene.abi==FX_SCENE_ABI && scene.page==1u && scene.width==1280u && scene.height==800u);
    assert(scene.count>1u && scene.count<=FX_SCENE_COMMANDS && scene.commands);
    const FxSceneCommand *commands=(const void *)(uintptr_t)scene.commands;
    assert(commands[0].kind==FX_SCENE_CLEAR);
    /* Exercise a legitimate native BUSY reply before the frame succeeds. */
    if(!submit_attempts++)return FX_BUSY;
    ++frames;return FX_OK;
}
static const FxPlatform platform={.abi=FX_PLATFORM_ABI,.count=1u,
    .model="Nikon Zf",.firmware="C3.01",.bindings=bindings,.io={.resource=resource}};
const FxPlatform *fx_platform_target(void){return &platform;}
FxNativeUiDispatch *fx_native_ui_dispatch_domain(void){return &dispatch;}
void fx_native_ui_refresh_owner(FxPlatformOwner *owner){assert(owner);++refreshes;}
FxZfLoadedModules *fx_zf_owner_modules(void){return 0;}

static void step(FxZfServices *services,uint32_t event,uint64_t now) {
    assert(fx_zf_services_prepare(services,now,event,0u)==FX_OK);
    assert(fenestra_module_entry(&services->module.base)==0);
    uint32_t sequence=services->exchange.request.sequence;
    assert(sequence);
    assert(fx_zf_services_dispatch_pending(services)==FX_OK);
    assert(services->exchange.reply.sequence==sequence);
    assert(services->exchange.reply.result==FX_OK || services->exchange.reply.result==FX_BUSY ||
        services->exchange.reply.result==FX_EMPTY);
    assert(fx_zf_services_dispatch_pending(services)==FX_EMPTY);
}
int main(void) {
    static FxZfServices services;
    assert(fx_zf_services_init(&services,1u,1u,FX_SDK_CAPS)==FX_OK);
    assert(services.platform.accounting==&services.accounting.state);
    uint64_t account_cookie=0u;
    assert(fx_account_resource(0,FX_RESOURCE_ATTACH,0,&services.platform,0u,&account_cookie,0,0)==FX_OK);
    FxServiceRequest account_request={0};FxServiceReply account_reply={0};
    account_request.arg[2]=FX_ACCOUNT_QUERY;
    assert(fx_account_resource(0,FX_RESOURCE_CALL,0,&services.platform,0u,&account_cookie,
        &account_request,&account_reply)==FX_OK);
    FxAccountInfo account_info;memcpy(&account_info,account_reply.data,sizeof(account_info));
    assert(account_info.owner==1u && account_info.generation==1u && account_info.rows==2u);
    assert(fx_account_resource(0,FX_RESOURCE_DETACH,0,&services.platform,0u,&account_cookie,0,0)==FX_OK);
    for(uint32_t i=0;i<8u;++i)step(&services,i?FX_EVENT_TICK:FX_EVENT_INIT,i);
    assert(attach_calls==1u && services.platform.control_count==1u && refreshes);
    assert(!frames && services.platform.controls[0].kind==FX_UI_PAGE);
    uint64_t focus=0;
    assert(fx_ui_focus_open(&services.platform,1u,&focus)==FX_OK && focus);
    for(uint32_t i=8u;i<24u;++i)step(&services,FX_EVENT_TICK,i);
    assert(frames>1u && submit_attempts>frames);
    assert(fx_ui_focus_close(&services.platform,focus)==FX_OK);

    /* SDK core query and empty-event replies also carry sequence/result. */
    FxServiceRequest *q=&services.exchange.request;
    memset(q,0,sizeof(*q));q->sequence=services.dispatched_sequence+1u;
    q->generation=1u;q->operation=FX_S_QUERY;
    assert(fx_zf_services_dispatch_pending(&services)==FX_OK);
    assert(services.exchange.reply.sequence==q->sequence && services.exchange.reply.result==FX_OK);
    assert(services.exchange.reply.value[1]==1u);
    ++q->sequence;q->operation=FX_S_EVENT;
    assert(fx_zf_services_dispatch_pending(&services)==FX_OK);
    assert(services.exchange.reply.value[0]==FX_EVENT_INIT);
    ++q->sequence;
    assert(fx_zf_services_dispatch_pending(&services)==FX_OK);
    assert(services.exchange.reply.sequence==q->sequence && services.exchange.reply.result==FX_EMPTY);
    ++q->sequence;q->generation=2u;
    assert(fx_zf_services_dispatch_pending(&services)==FX_OK);
    assert(services.exchange.reply.result==FX_STALE);
    /* Zf binds the original schedule provider to the same owner wake port. */
    uint64_t cookie=0u;
    assert(fx_schedule_resource(FX_RESOURCE_ATTACH,&services.platform,&cookie,0,0)==FX_OK);
    uint64_t ticket=fx_worker_wait_ticket(&services.wait);
    FxScheduleWait wait={ticket,0u,0u};FxServiceRequest schedule={0};FxServiceReply waited={0};
    schedule.arg[2]=FX_SCHEDULE_WAIT;schedule.bytes=sizeof(wait);
    memcpy(schedule.data,&wait,sizeof(wait));
    assert(fx_schedule_resource(FX_RESOURCE_CALL,&services.platform,&cookie,&schedule,&waited)==FX_OK);
    fx_worker_wait_signal(&services.wait);
    assert(fx_worker_wait_continue(&services.wait,1u,&waited,&services.protocol)==FX_OK);
    assert(waited.value[0]>ticket && waited.value[1]==1u && !waited.value[2]);
    assert(services.wait.resumed && !services.wait.armed);
    assert(fx_schedule_resource(FX_RESOURCE_DETACH,&services.platform,&cookie,0,0)==FX_OK);
    assert(fx_zf_services_stop(&services,0u)==FX_OK);
    assert(!services.protocol.active && services.protocol.stop_state==FX_SERVICE_STOP_DONE);
    assert(fx_zf_services_stop(&services,0u)==FX_OK);
    printf("PASS unchanged demo -> Zf SDK replies -> UI page/focus -> scene frames=%u BUSY retry; core event/empty/stale replies\n",frames);
    return 0;
}
