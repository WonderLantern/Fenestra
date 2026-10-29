#include "fenestra_native_scene.h"
#include "fenestra_platform_native.h"
#include "fenestra_native_namespace.h"
#include "fenestra_native_display.h"
#include "fenestra_native_ui.h"
#include "fenestra_native_menu.h"
#include "fenestra_module_manager.h"
#include "fenestra_hooks.h"
#include "fenestra_memory.h"

#include <stddef.h>
/* Target-neutral, read-only layout for owned laboratory observation. */
const uint32_t fx_native_scene_metadata[]={0x46585343u,3u,
    offsetof(FxPlatformOwner,provider_contexts),offsetof(FxProviderContext,next),
    offsetof(FxProviderContext,name),offsetof(FxProviderContext,context),
    offsetof(FxNativeSceneBudget,head),offsetof(FxNativeSceneState,next),
    offsetof(FxNativeSceneState,gate),offsetof(FxNativeSceneState,page),
    offsetof(FxNativeSceneState,retired),offsetof(FxNativeSceneState,error),
    offsetof(FxNativeSceneState,display)+offsetof(FxNativeDisplay,attached),
    offsetof(FxNativeSceneState,display)+offsetof(FxNativeDisplay,frames),
    offsetof(FxNativeSceneState,display)+offsetof(FxNativeDisplay,readers),
    offsetof(FxNativeSceneState,store)+offsetof(FxSceneStore,frames),
    offsetof(FxNativeSceneBudget,active)};
/* Published nodes and their code/data are never recycled before reboot.
 * GUI callbacks only follow immutable next links and atomic page/gate. */
static FxNativeSceneBudget *scene_budget(FxPlatformOwner *owner) {
    static const char name[]="camera.display.scenes";
    for(FxProviderContext *node=atomic_load_explicit(&owner->provider_contexts,memory_order_acquire);node;node=node->next) {
        if(!node->name)continue;
        uint32_t i=0u;while(i<sizeof(name) && node->name[i]==name[i])++i;
        if(i==sizeof(name))return node->context;
    }
    return 0;
}
static uintptr_t function(FxPlatformOwner *o,const char *name) {
    const FxNamedBinding *b=fx_platform_find(o,name);
    return b && b->kind==FX_BIND_FUNCTION && b->abi==FX_CALL_U64_8?(uintptr_t)b->address:0;
}
static void retained_allocation(FxNativeSceneState *s,uint32_t bit,uint32_t bytes) {
    if(!(s->retained_mask&bit)){s->retained_mask|=bit;fx_allocation_observe(s->observer,bytes,FX_ALLOC_PUBLISHED);}
}
static void retain_storage(FxNativeSceneState *s) {
    retained_allocation(s,1u,(uint32_t)sizeof(*s));
    for(uint32_t i=0u;i<2u;++i) {
        if(s->store.scene[i].commands)retained_allocation(s,2u<<i,s->store.scene[i].capacity*(uint32_t)sizeof(FxSceneCommand));
        if(s->store.scene[i].data)retained_allocation(s,8u<<i,s->store.scene[i].data_capacity);
    }
}
static void *request_storage(FxPlatformOwner *owner,FxNativeSceneBudget *budget,uint32_t bytes) {
    if(bytes>FX_NATIVE_SCENE_BUDGET-budget->used)return 0;
    void *p=((void *(*)(uint32_t,uint32_t))function(owner,"rtos.memory.allocate"))(bytes,0u);
    if(p){budget->used+=bytes;if(budget->peak<budget->used)budget->peak=budget->used;
        fx_allocation_observe(budget->observer,bytes,FX_ALLOC_REQUESTED);}
    return p;
}
static void release_storage(FxNativeSceneState *s,void *p,uint32_t bytes,uint32_t bit) {
    FxAllocationObserver observer=s->observer;uint32_t retained=s->retained_mask&bit;s->retained_mask&=~bit;
    FxNativeSceneBudget *budget=s->budget;
    ((void (*)(void *))function(s->owner,"rtos.memory.release"))(p);
    budget->used-=bytes;
    fx_allocation_observe(observer,bytes,retained?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);
}
static void revoke(FxNativeSceneState *s) {
    uint64_t old=atomic_load_explicit(&s->gate,memory_order_acquire),next;
    do {next=old>=UINT64_MAX-2u?UINT64_MAX-1u:(old|1u)+1u;}
    while(!atomic_compare_exchange_weak_explicit(&s->gate,&old,next,memory_order_acq_rel,memory_order_acquire));
}
void fx_native_scene_closed(FxPlatformOwner *owner,uint32_t page) {
    if(!owner)return;
    FxNativeSceneBudget *budget=scene_budget(owner);if(!budget)return;
    FxNativeSceneState *s=atomic_load_explicit(&budget->active,memory_order_acquire);
    if(s && atomic_load_explicit(&s->page,memory_order_acquire)==page){revoke(s);fx_platform_wake(owner);}
}
static FxResult hide(FxNativeSceneState *s) {
    revoke(s);
    if(atomic_load_explicit(&s->display.readers,memory_order_acquire))return FX_BUSY;
    fx_native_display_detach(&s->display);
    if(atomic_load_explicit(&s->display.attached,memory_order_acquire))return FX_BUSY;
    FxNativeSceneState *expected=s;
    (void)atomic_compare_exchange_strong_explicit(&s->budget->active,&expected,0,memory_order_release,memory_order_relaxed);
    return FX_OK;
}
int fx_native_scene_needs_poll(FxPlatformOwner *owner) {
    if(!owner)return 0;
    FxNativeSceneBudget *budget=scene_budget(owner);if(!budget)return 0;
    return atomic_load_explicit(&budget->active,memory_order_acquire)!=0;
}
void fx_native_scene_poll(FxPlatformOwner *owner) {
    if(!owner || !owner->owner || owner->owner>FX_MODULE_OWNER_LIMIT)return;
    FxNativeSceneBudget *budget=scene_budget(owner);if(!budget)return;
    FxNativeSceneState *s=atomic_load_explicit(&budget->active,memory_order_acquire);
    if(s && (s->retired || !(atomic_load_explicit(&s->gate,memory_order_acquire)&1u) ||
       !fx_native_display_is_current(&s->display)))(void)hide(s);
}
static FxResult publish(FxNativeSceneState *s,uint32_t page) {
    if(s->registered){atomic_store_explicit(&s->page,page,memory_order_release);return FX_OK;}
    FxResult r=FX_OK;
    if(!s->reference){r=fx_ui_reference(s->owner,1);if(r!=FX_OK)return r;s->reference=1u;}
    atomic_store_explicit(&s->page,page,memory_order_relaxed);
    /* One serialized worker owns each owner. GUI may read but never inserts. */
    s->next=atomic_load_explicit(&s->budget->head,memory_order_relaxed);
    atomic_store_explicit(&s->budget->head,s,memory_order_release);
    ++s->budget->published;
    s->registered=1u;
    retain_storage(s);
    return FX_OK;
}
static FxResult prepare_storage(FxNativeSceneState *s,uint32_t count,uint32_t data_bytes) {
    uint32_t bank=s->store.front^1u;FxScene *next=&s->store.scene[bank];
    uint32_t capacity=next->capacity;
    if(capacity<count){capacity=16u;while(capacity<count)capacity*=2u;}
    uint32_t command_bytes=capacity*(uint32_t)sizeof(FxSceneCommand);
    FxSceneCommand *commands=0;uint8_t *data=0;
    if(capacity>next->capacity) {
        commands=request_storage(s->owner,s->budget,command_bytes);if(!commands)return FX_BUDGET;
    }
    if(data_bytes>next->data_capacity) {
        data=request_storage(s->owner,s->budget,data_bytes);
        if(!data){if(commands)release_storage(s,commands,command_bytes,0u);return FX_BUDGET;}
    }
    /* Both allocations exist before replacing the non-front bank. The gate
     * is revoked and all display readers have left; no callback sees a freed bank. */
    if(commands) {
        uint32_t bit=2u<<bank;
        if(next->commands)release_storage(s,next->commands,next->capacity*(uint32_t)sizeof(FxSceneCommand),bit);
        next->commands=commands;next->capacity=capacity;
        if(s->registered)retained_allocation(s,bit,command_bytes);
    }
    if(data) {
        uint32_t bit=8u<<bank;
        if(next->data)release_storage(s,next->data,next->data_capacity,bit);
        next->data=data;next->data_capacity=data_bytes;
        if(s->registered)retained_allocation(s,bit,data_bytes);
    }
    return FX_OK;
}
static void restore_gate(FxNativeSceneState *s,uint64_t disabled,uint64_t enabled) {
    (void)atomic_compare_exchange_strong_explicit(&s->gate,&disabled,enabled,memory_order_release,memory_order_relaxed);
}
static FxResult submit(FxNativeSceneState *s,const FxServiceRequest *q,FxServiceReply *a) {
    if((q->bytes!=sizeof(FxSceneSubmission) && q->bytes!=sizeof(FxSceneImageSubmission)) || q->arg[3])return FX_ARGUMENT;
    FxSceneSubmission request;memcpy(&request,q->data,sizeof(request));
    if(request.bytes!=q->bytes)return FX_ARGUMENT;
    if(request.abi!=FX_SCENE_ABI || !request.page)return FX_ARGUMENT;
    if(!request.count || request.count>FX_SCENE_COMMANDS || !request.width || !request.height ||
       request.width>FX_SCENE_EXTENT || request.height>FX_SCENE_EXTENT)return FX_BOUNDS;
    FxSceneImageSubmission images={0};
    if(q->bytes==sizeof(images)) {
        if(!(fx_native_display_capabilities()&FX_SCENE_CAP_IMAGE))return FX_CAPABILITY;
        memcpy(&images,q->data,sizeof(images));
        if(images.reserved || request.abi!=FX_SCENE_ABI)return FX_ARGUMENT;
        if(!images.data_bytes || images.data_bytes>FX_SCENE_DATA_BYTES)return FX_BOUNDS;
    }
    uint32_t page=request.page;void *native_page=0;FxResult result;
    if(page==FX_SCENE_GLOBAL_PAGE) {
        if(!(fx_native_display_capabilities()&FX_SCENE_CAP_GLOBAL))return FX_CAPABILITY;
        if(!fx_native_display_global_ready())return FX_STATE;
    } else {
        uint32_t slot;result=fx_ui_page_slot(s->owner,request.page,&slot);if(result!=FX_OK)return result;
        FxNativeUi *ui=(FxNativeUi *)atomic_load_explicit(&s->owner->native_ui,memory_order_acquire);
        if(!ui || ui->owner!=s->owner)return FX_STATE;
        page=ui->page_base+slot;
        native_page=fx_native_display_current_page(page);if(!native_page)return FX_STATE;
    }
    /* A module can open several contexts but only one may paint a page at a
     * time. This is resource ownership, not a debugger admission rule. */
    FxNativeSceneState *active=atomic_load_explicit(&s->budget->active,memory_order_acquire);
    if(active && active!=s)return FX_BUSY;
    uint64_t old=atomic_load_explicit(&s->gate,memory_order_acquire),disabled=old&~UINT64_C(1);
    if(old>=UINT64_MAX-2u)return FX_BUDGET;
    if(!atomic_compare_exchange_strong_explicit(&s->gate,&old,disabled,memory_order_acq_rel,memory_order_acquire))return FX_BUSY;
    if(atomic_load_explicit(&s->display.readers,memory_order_acquire)){restore_gate(s,disabled,old);return FX_BUSY;}
    result=prepare_storage(s,request.count,images.data_bytes);
    if(result!=FX_OK){restore_gate(s,disabled,old);return result;}
    if(images.data_bytes)result=fx_scene_stage_images(&s->store,s->owner,&images);
    else result=fx_scene_stage(&s->store,s->owner,&request);
    if(result!=FX_OK){restore_gate(s,disabled,old);return result;}
    if(s->display.attached && (s->display.page!=native_page || !fx_native_display_is_current(&s->display))) {
        fx_native_display_detach(&s->display);
        if(s->display.attached)return FX_BUSY;
    }
    result=publish(s,page);if(result!=FX_OK){restore_gate(s,disabled,old);return result;}
    const FxScene *candidate=fx_scene_candidate(&s->store);
    atomic_store_explicit(&s->budget->active,s,memory_order_release);
    if(!s->display.attached)result=fx_native_display_attach(&s->display,native_page,candidate);
    if(result!=FX_OK)return result;
    s->display.scene=candidate;
    uint64_t expected=disabled;
    if(!atomic_compare_exchange_strong_explicit(&s->gate,&expected,disabled|1u,memory_order_release,memory_order_relaxed))return FX_STATE;
    result=fx_native_display_present(&s->display,candidate);
    if(result==FX_OK) {
        fx_scene_commit(&s->store);a->value[0]=s->store.frames;
        (void)fx_ui_poll_message(s->owner,a->data,sizeof(a->data),&a->bytes);
    } else {
        (void)hide(s);
        if(result!=FX_STATE){s->error=(uint32_t)result;fx_native_module_fault(s->owner,result);}
    }
    return result;
}
FxResult fx_native_scene_resource(void *context,uint32_t op,const FxNamedBinding *b,FxPlatformOwner *owner,uint64_t token,
    uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)b;
    if(!owner || !owner->owner || owner->owner>FX_MODULE_OWNER_LIMIT || !cookie)return FX_ARGUMENT;
    if(op==FX_RESOURCE_ATTACH) {
        uintptr_t allocate=function(owner,"rtos.memory.allocate"),release=function(owner,"rtos.memory.release");
        if(!allocate || !release)return FX_CAPABILITY;
        FxNativeSceneBudget *budget=scene_budget(owner);
        if(!budget) {
            budget=((void *(*)(uint32_t,uint32_t))allocate)((uint32_t)sizeof(*budget),0u);if(!budget)return FX_BUDGET;
            memset(budget,0,sizeof(*budget));budget->observer=fx_platform_account_observer(owner,"scene.storage");
            budget->retained=(FxProviderContext){.name="camera.display.scenes",.context=budget};
            atomic_init(&budget->head,0);atomic_init(&budget->active,0);
            FxResult result=fx_platform_retain_context(owner,&budget->retained);
            if(result!=FX_OK){((void (*)(void *))release)(budget);return result;}
            budget->used=budget->peak=sizeof(*budget);
            fx_allocation_observe(budget->observer,(uint32_t)sizeof(*budget),FX_ALLOC_REQUESTED);
            fx_allocation_observe(budget->observer,(uint32_t)sizeof(*budget),FX_ALLOC_PUBLISHED);
        }
        FxNativeSceneState *s=request_storage(owner,budget,(uint32_t)sizeof(*s));if(!s)return FX_BUDGET;
        memset(s,0,sizeof(*s));s->owner=owner;s->token=token;s->observer=budget->observer;s->budget=budget;++budget->contexts;
        fx_scene_init(&s->store,0,0);
        atomic_init(&s->gate,0u);atomic_init(&s->page,0u);atomic_init(&s->display.readers,0u);
        s->display.gate=&s->gate;s->display.exact_colors=1u;*cookie=(uintptr_t)s;return FX_OK;
    }
    FxNativeSceneState *s=(FxNativeSceneState *)(uintptr_t)*cookie;
    if(!s || s->owner!=owner || s->token!=token)return FX_STALE;
    if(op==FX_RESOURCE_PANIC){s->retired=1u;revoke(s);retain_storage(s);return FX_OK;}
    if(op==FX_RESOURCE_DETACH) {
        s->retired=1u;FxResult result=hide(s);if(result!=FX_OK)return result;
        if(s->reference){result=fx_ui_reference(owner,0);if(result!=FX_OK)return result;s->reference=0u;}
        if(!s->registered) {
            uintptr_t release=function(owner,"rtos.memory.release");if(!release)return FX_CAPABILITY;
            for(uint32_t i=0u;i<2u;++i) {
                if(s->store.scene[i].data)release_storage(s,s->store.scene[i].data,s->store.scene[i].data_capacity,8u<<i);
                if(s->store.scene[i].commands)release_storage(s,s->store.scene[i].commands,s->store.scene[i].capacity*(uint32_t)sizeof(FxSceneCommand),2u<<i);
            }
            --s->budget->contexts;
            release_storage(s,s,(uint32_t)sizeof(*s),1u);
        }
        *cookie=0u;return FX_OK;
    }
    if(op!=FX_RESOURCE_CALL || !q || !a || s->retired || s->error)return FX_STATE;
    if(q->arg[2]==FX_SCENE_SUBMIT)return submit(s,q,a);
    if(q->bytes || q->arg[3])return FX_ARGUMENT;
    if(q->arg[2]==FX_SCENE_HIDE)return hide(s);
    if(q->arg[2]==FX_SCENE_QUERY) {
        uint32_t caps=fx_native_display_capabilities();
        FxSceneInfo info={FX_SCENE_ABI,sizeof(info),FX_SCENE_COMMANDS,FX_SCENE_EXTENT,14u|((caps&FX_SCENE_CAP_IMAGE)?16u:0u),1u,
            FX_NATIVE_SCENE_BUDGET/(uint32_t)sizeof(*s),caps|FX_SCENE_CAP_CAPACITY};
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(q->arg[2]==FX_SCENE_STATUS) {
        a->value[0]=s->store.frames;a->value[1]=atomic_load(&s->display.attached);
        a->value[2]=atomic_load(&s->gate)&1u;a->value[3]=s->error;return FX_OK;
    }
    if(q->arg[2]==FX_SCENE_CAPACITY) {
        FxNativeSceneBudget *budget=s->budget;
        FxSceneCapacity info={FX_SCENE_ABI,sizeof(info),FX_SCENE_CAPACITY_PARTIAL,0u,
            FX_NATIVE_SCENE_BUDGET,budget->used,budget->peak,budget->contexts,budget->published,sizeof(*s)};
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    return FX_ARGUMENT;
}
