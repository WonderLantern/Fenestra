#include "fenestra_document_sdk.h"
static FxSdkClient client;
static uint32_t stage,offset,chunk;
static uint64_t document,upload;
static const char text[]=
#define LINE "C2.01 native document control. This copied paragraph tests measured pagination and navigation. No camera setting is changed.\n\n"
 LINE LINE LINE LINE LINE LINE LINE LINE LINE LINE LINE LINE LINE LINE LINE LINE
#undef LINE
;
int fenestra_module_entry(const FxModuleServices *base) {
 if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
 const FxServiceModule *api=(const FxServiceModule *)base;
 if(client.pending) {
  FxServiceReply reply;FxResult r=fx_sdk_take(&client,api,&reply);if(r==FX_BUSY)return 0;
  if(r!=FX_OK || (reply.result!=FX_OK && !(stage==8u && reply.result==FX_EMPTY)))return 100+(int)stage;
  if(stage==4u)document=reply.value[0];
  if(stage==5u)upload=reply.value[0];
  if(stage==6u){offset+=chunk;if(offset<sizeof(text)-1u)return 0;}
  if(stage==8u){
   if(reply.result==FX_OK){if(reply.bytes!=sizeof(FxUiEvent))return 2;
    const FxUiEvent *e=(const void *)reply.data;
    if(e->control==3u && e->event==FX_INPUT_ACTIVATE)stage=9u;
   }
  } else ++stage;
 }
 FxResult r;uint64_t args[4]={0};
 if(stage==0u){args[0]=FX_LIFE_CONFIGURE;args[1]=FX_MODULE_APP;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
 else if(stage>=1u && stage<=3u) {
  FxUiControl c={.abi=1u,.bytes=sizeof(c),.id=stage,.parent=stage==1u?0u:1u,.kind=stage==1u?FX_UI_PAGE:stage==2u?FX_UI_DOCUMENT:FX_UI_ACTION,.flags=FX_UI_ENABLED|FX_UI_VISIBLE|(stage==1u?FX_UI_ROOT:0u)};
  const char *label=stage==1u?"Z6III document control":stage==2u?"Read document":"Finish document test";
  for(uint32_t i=0;label[i];++i)c.label[i]=label[i];r=fx_sdk_ui(&client,api,&c);
 } else if(stage==4u)r=fx_sdk_open(&client,api,"core.ui.document",1u);
 else if(stage==5u)r=fx_sdk_document(&client,api,document,FX_DOCUMENT_BEGIN,2,0,0,sizeof(text)-1u,0);
 else if(stage==6u){chunk=(uint32_t)sizeof(text)-1u-offset;if(chunk>200u)chunk=200u;r=fx_sdk_document(&client,api,document,FX_DOCUMENT_WRITE,2,upload,offset,chunk,text+offset);}
 else if(stage==7u)r=fx_sdk_document(&client,api,document,FX_DOCUMENT_COMMIT,2,upload,0,0,0);
 else if(stage==8u)r=fx_sdk_events(&client,api);
 else if(stage==9u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,document,0,0,0,0);
 else {args[0]=FX_LIFE_FINISH;r=fx_sdk_submit(&client,api,FX_S_LIFECYCLE,args,0,0);}
 return r==FX_OK?0:3;
}
