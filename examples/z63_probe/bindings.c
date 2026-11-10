/* Open/close the cross-camera raw SDK contract and copy its native tables.
 * Native singleton editor operations are deliberately not invoked off UITask. */
#include "fenestra_sdk.h"
static FxSdkClient client;
static uint32_t phase,index;
static uint64_t handle;
static const char *const names[]={
    "camera.menu.page.get","camera.menu.text.get","camera.ui.editor.init","camera.ui.editor.read",
    "camera.ui.editor.insert-byte","camera.ui.editor.delete-byte","camera.ui.editor.hit",
    "camera.ui.text.measure","camera.ui.text.align","camera.ui.bounds.get","camera.ui.bounds.set",
    "rtos.spinlock.init","rtos.mmu.context.walk","camera.ui.editor.page.build",
    "camera.ui.editor.template","camera.menu.page-roots","camera.locale.font-root",
    "rtos.mmu.context","rtos.irq.controller"};
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply a;FxResult r=fx_sdk_take(&client,api,&a);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || a.result!=FX_OK)return 100+(int)phase;
        if(phase==0u)phase=1u;
        else if(phase==1u){handle=a.value[0];if(!handle)return 2;phase=index>=14u?2u:3u;}
        else if(phase==2u){if(a.bytes!=(index==16u?8u:16u))return 3;phase=3u;}
        else if(phase==3u){++index;phase=1u;}
    }
    FxResult r;
    if(phase==0u)r=fx_sdk_lifecycle(&client,api,FX_LIFE_CONFIGURE,FX_MODULE_APP);
    else if(index>=sizeof(names)/sizeof(*names))r=fx_sdk_lifecycle(&client,api,FX_LIFE_FINISH,0u);
    else if(phase==1u)r=fx_sdk_open(&client,api,names[index],1u);
    else if(phase==2u)r=fx_sdk_platform(&client,api,FX_BIND_COPY,handle,0u,index==16u?8u:16u,0,0);
    else r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,handle,0u,0u,0,0);
    return r==FX_OK?0:4;
}
