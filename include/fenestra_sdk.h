#ifndef FENESTRA_SDK_H
#define FENESTRA_SDK_H
#include "fenestra_platform.h"
#include "fenestra_schedule.h"
#include "fenestra_capacity.h"
#include "fenestra_resources.h"
#include "fenestra_module_bus.h"
/* Persist this in module-owned data. Submit at most one copied request then
 * return from the entry; take its reply on a later invocation. No busy wait,
 * synchronous SVC, native pointer callback or core memory sharing is needed. */
typedef struct {uint32_t sequence,pending;} FxSdkClient;
static inline FxResult fx_sdk_submit(FxSdkClient *c,const FxServiceModule *api,uint32_t operation,
    const uint64_t args[4],const void *data,uint32_t bytes) {
    if(!c || !api || !api->exchange || api->service_abi!=FX_SERVICE_ABI || bytes>FX_SERVICE_BYTES || (bytes && !data))return FX_ARGUMENT;
    if(c->pending)return FX_BUSY;
    if(c->sequence==UINT32_MAX)return FX_BUDGET;
    FxServiceRequest *q=&api->exchange->request;
    q->sequence=++c->sequence;q->generation=api->generation;q->operation=operation;q->bytes=bytes;
    for(uint32_t i=0;i<4u;++i)q->arg[i]=args?args[i]:0u;
    const uint8_t *p=data;for(uint32_t i=0;i<bytes;++i)q->data[i]=p[i];
    for(uint32_t i=bytes;i<FX_SERVICE_BYTES;++i)q->data[i]=0;
    c->pending=q->sequence;return FX_OK;
}
static inline FxResult fx_sdk_take(FxSdkClient *c,const FxServiceModule *api,FxServiceReply *out) {
    if(!c || !api || !api->exchange || !out || !c->pending)return FX_STATE;
    const FxServiceReply *r=&api->exchange->reply;if(r->sequence!=c->pending)return FX_BUSY;
    if(r->bytes>FX_SERVICE_BYTES)return FX_INTEGRITY;
    *out=*r;c->pending=0u;return FX_OK;
}
static inline FxResult fx_sdk_platform(FxSdkClient *c,const FxServiceModule *a,uint32_t op,uint64_t handle,
    uint64_t subop,uint64_t length,const void *data,uint32_t bytes) {
    const uint64_t args[4]={op,handle,subop,length};return fx_sdk_submit(c,a,FX_S_PLATFORM,args,data,bytes);
}
static inline FxResult fx_sdk_open(FxSdkClient *c,const FxServiceModule *a,const char *name,uint32_t version) {
    char copied[64]={0};if(!name)return FX_ARGUMENT;
    uint32_t n=0;while(n<64u && name[n])++n;if(!n || n==64u)return FX_ARGUMENT;
    for(uint32_t i=0;i<n;++i)copied[i]=name[i];
    return fx_sdk_platform(c,a,FX_BIND_OPEN,version,0u,0u,copied,sizeof(copied));
}
static inline FxResult fx_sdk_wait(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,uint64_t observed,uint32_t ticks) {
    FxScheduleWait r={observed,ticks,0u};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,FX_SCHEDULE_WAIT,0u,&r,sizeof(r));
}
/* QUERY then read the scheduler ticket BEFORE WATCH; WAIT that old ticket.
 * Both a release before WATCH and one before WAIT produce an immediate retry.
 * A wake is advisory, not an exclusive reservation of native capacity. */
static inline FxResult fx_sdk_capacity(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint32_t operation,const char *name,uint64_t observed) {
    FxCapacityRequest r={.observed=observed};if(!name)return FX_ARGUMENT;
    uint32_t n=0u;while(n<sizeof(r.name) && name[n])++n;if(!n || n==sizeof(r.name))return FX_ARGUMENT;
    for(uint32_t i=0u;i<n;++i)r.name[i]=name[i];
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,operation,0u,&r,sizeof(r));
}
static inline FxResult fx_sdk_open_instance(FxSdkClient *c,const FxServiceModule *a,const char *name,uint32_t version) {
    FxResult r=fx_sdk_open(c,a,name,version);if(r==FX_OK)a->exchange->request.arg[2]=1u;return r;
}
/* Optional target-specific bindings are copied into this owner, immutable
 * until reboot. Their ABI/preimage evidence is the module author's duty. */
static inline FxResult fx_sdk_declare(FxSdkClient *c,const FxServiceModule *a,const FxBindingInfo *binding) {
    const uint64_t args[4]={FX_BIND_DECLARE,0u,0u,0u};return fx_sdk_submit(c,a,FX_S_PLATFORM,args,binding,sizeof(*binding));
}
static inline FxResult fx_sdk_call_frame(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,const FxNativeCallFrame *frame) {
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,0u,0u,frame,sizeof(*frame));
}
static inline FxResult fx_sdk_memory_allocate(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,uint32_t bytes,uint32_t export_address) {
    FxMemoryRequest r={.bytes=bytes};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,export_address?FX_MEMORY_ALLOCATE:FX_MEMORY_COPY_ALLOCATE,0u,&r,sizeof(r));
}
static inline FxResult fx_sdk_memory_release(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,uint64_t buffer,uint32_t native_quiesced) {
    FxMemoryRequest r={.handle=buffer};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,native_quiesced?FX_MEMORY_RECLAIM:FX_MEMORY_RELEASE,0u,&r,sizeof(r));
}
static inline FxResult fx_sdk_memory_module(FxSdkClient *c,const FxServiceModule *a,uint64_t binding,
    uint64_t buffer,uint32_t offset,void *module_address,uint32_t bytes,uint32_t write_to_module) {
    if(!module_address || !bytes || write_to_module>1u)return FX_ARGUMENT;
    FxModuleMemoryRequest r={buffer,bytes,offset,(uintptr_t)module_address};
    return fx_sdk_platform(c,a,FX_BIND_CALL,binding,write_to_module?FX_MEMORY_TO_MODULE:FX_MEMORY_FROM_MODULE,0u,&r,sizeof(r));
}
static inline FxResult fx_sdk_ui(FxSdkClient *c,const FxServiceModule *a,const FxUiControl *control) {
    const uint64_t args[4]={FX_UI_PUT,0u,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,control,sizeof(*control));
}
static inline FxResult fx_sdk_ui_get(FxSdkClient *c,const FxServiceModule *a,uint32_t control) {
    const uint64_t args[4]={FX_UI_GET,control,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
static inline FxResult fx_sdk_ui_hide(FxSdkClient *c,const FxServiceModule *a,uint32_t control) {
    const uint64_t args[4]={FX_UI_HIDE,control,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
static inline FxResult fx_sdk_events(FxSdkClient *c,const FxServiceModule *a) {
    const uint64_t args[4]={FX_UI_POLL,0u,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
/* Zero queries; nonzero reserves a power-of-two capacity, or shrinks when
 * queued events fit. Requests 1..16 return to inline storage. No GUI allocation. */
static inline FxResult fx_sdk_ui_queue(FxSdkClient *c,const FxServiceModule *a,uint32_t events) {
    const uint64_t args[4]={FX_UI_QUEUE,events,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
static inline FxResult fx_sdk_ui_control_capacity(FxSdkClient *c,const FxServiceModule *a) {
    const uint64_t args[4]={FX_UI_CONTROL_CAPACITY,0u,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
/* Move within the existing parent, before another live sibling; before=0
 * appends after the last sibling. Works beyond one message of IDs. */
static inline FxResult fx_sdk_ui_move(FxSdkClient *c,const FxServiceModule *a,uint32_t control,uint32_t before) {
    const uint64_t args[4]={FX_UI_MOVE,control,before,0u};return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
static inline FxResult fx_sdk_ui_remove(FxSdkClient *c,const FxServiceModule *a,uint32_t control) {
    const uint64_t args[4]={FX_UI_REMOVE,control,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
static inline FxResult fx_sdk_ui_reorder(FxSdkClient *c,const FxServiceModule *a,uint32_t parent,const uint32_t *ids,uint32_t count) {
    if(count>FX_SERVICE_BYTES/sizeof(uint32_t) || (count && !ids))return FX_ARGUMENT;
    const uint64_t args[4]={FX_UI_REORDER,parent,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,ids,count*4u);
}
static inline FxResult fx_sdk_ui_status(FxSdkClient *c,const FxServiceModule *a) {
    const uint64_t args[4]={FX_UI_STATUS,0u,0u,0u};return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
/* Available with FX_UI_CAP_EVENT_REVISION. Focus/blur carry a focus epoch,
 * not a control definition revision. Zero denotes the legacy input route. */
static inline uint32_t fx_sdk_event_revision(const FxUiEvent *e) {
    return e && ((e->event>=FX_INPUT_ACTIVATE && e->event<=FX_INPUT_CANCEL) || e->event==FX_INPUT_TEXT)?e->reserved:0u;
}
/* Accept either UI POLL or a successful scene SUBMIT reply. Copy before the
 * next SDK reply replaces its storage. No application state is overwritten. */
static inline FxResult fx_sdk_text_event(const FxServiceReply *reply,FxUiTextEvent *out) {
    if(!reply || !out)return FX_ARGUMENT;
    if(reply->result!=FX_OK || reply->bytes!=sizeof(*out))return FX_EMPTY;
    FxUiEvent event;uint8_t *prefix=(uint8_t *)&event;
    for(uint32_t i=0;i<sizeof(event);++i)prefix[i]=reply->data[i];
    if(event.event!=FX_INPUT_TEXT)return FX_EMPTY;
    if(event.value>=FX_UI_INPUT_TEXT || reply->data[sizeof(event)+event.value])return FX_INTEGRITY;
    for(uint32_t i=0;i<event.value;++i)if(!reply->data[sizeof(event)+i])return FX_INTEGRITY;
    uint8_t *destination=(uint8_t *)out;
    for(uint32_t i=0;i<sizeof(*out);++i)destination[i]=reply->data[i];
    return FX_OK;
}
static inline FxResult fx_sdk_ui_query(FxSdkClient *c,const FxServiceModule *a,uint32_t focus) {
    if(focus>1u)return FX_ARGUMENT;
    const uint64_t args[4]={focus?FX_UI_FOCUS:FX_UI_QUERY,0u,0u,0u};
    return fx_sdk_submit(c,a,FX_S_UI,args,0,0u);
}
static inline FxResult fx_sdk_lifecycle(FxSdkClient *c,const FxServiceModule *a,uint32_t op,uint32_t kind) {
    const uint64_t args[4]={op,kind,0u,0u};return fx_sdk_submit(c,a,FX_S_LIFECYCLE,args,0,0u);
}
#endif
