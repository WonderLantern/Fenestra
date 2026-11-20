/* Native pages retain copied SDK state. Only the original input task builds
 * widgets or commits presentation; workers publish dispatcher requests. */
#include "ui.h"
#include "runtime.h"
#include "manager.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_ui_pager.h"
#include "fenestra_native_apps.h"
#include "fenestra_native_namespace.h"
#include "fenestra_accounting_service.h"
#include "fenestra_platform_native.h"
#include "fenestra_memory.h"
#include "fenestra_native_visibility.h"
#include "fenestra_native_text.h"
#include "fenestra_native_title.h"
#include "fenestra_native_ui_reader.h"
#include "fenestra_native_ui_editor.h"
#include "fenestra_native_display.h"
#define TEXT 3300u
#define MANAGER_TEXT (TEXT+10u)
#define MODULE_TEXT (MANAGER_TEXT+FX_NATIVE_UI_TEXTS+2u)
#define STRIDE (FX_NATIVE_UI_TEXTS+2u)
extern void *fx_z63_current_page(void);
extern uint64_t fx_z63_menu_navigate(uint32_t);
extern FxZ63Runtime *fx_z63_menu_runtime(void);
typedef struct Node {struct Node *next;FxNativeUi ui;uint32_t modal_target[3],modal_original,modal_owned;} Node;
struct FxZ63Ui {FxNativeMenuProfile profile;FxNativeUiDispatch dispatch;FxNativeUiSpace space;FxNativeApps apps;_Atomic(FxZ63Manager *) manager;FxCapacityRegistry capacity;FxNativeCapacityDomains domains;Node *nodes;uint32_t count;};
const uint32_t fx_z63_manager_layout[]={0x46584d36u,1u,offsetof(FxZ63Ui,manager),offsetof(FxNativeModules,ui)};
const uint32_t fx_z63_ui_layout[]={0x46585536u,1u,offsetof(FxZ63Ui,dispatch),offsetof(FxNativeUiDispatch,task),offsetof(FxNativeUiDispatch,tasks),offsetof(FxTaskDomain,task),offsetof(FxZ63Ui,nodes),offsetof(Node,ui)};
static void *allocate(uint32_t n){return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u)(n,0u);}
static void *map_allocate(void *c,uint32_t n){(void)c;return allocate(n);}
static void map_release(void *c,void *p){(void)c;((void (*)(void *))(uintptr_t)0x40d32d40u)(p);}
static FxResult ensure(void *c,uint32_t n){(void)c;return fx_z63_menu_text_ensure(n);}
static uint32_t noop(void *p){(void)p;return 1u;}
const FxNativeUiDispatchProfile fx_native_ui_dispatch_profile={(uintptr_t)fx_z63_current_page,0x403e9510u,0x403e9688u,16u,0u,0x403eddf0u,0x403edebcu,0x200000u,0u};
FxNativeUiDispatch *fx_native_ui_dispatch_domain(void){FxZ63Ui *s=fx_z63_menu_ui();return s?&s->dispatch:0;}
FxNativeApps *fx_native_apps_domain(void){FxZ63Ui *s=fx_z63_menu_ui();return s?&s->apps:0;}
FxResult fx_z63_ui_register(FxPlatformOwner *owner){
    FxZ63Ui *s=fx_z63_menu_ui();if(!s || !owner)return FX_STATE;
    owner->capacity=&s->capacity;return fx_ui_dispatch_register(&s->dispatch,owner);
}
static const FxNativeVisibilityProfile visibility={(uintptr_t)fx_z63_current_page,0x40331b28u,0x404054c8u,0x4032b3dcu,0x4032b8bcu,0x1c0u,0x4dc1u,0x4dc0u};
static const FxNativeLayoutProfile layout={(uintptr_t)fx_z63_current_page,0x403c0d9cu,0x403c0eacu,0x403c0ffcu,0x403c0e14u,0x403bf82cu,0x403bfaf0u,0x403c1290u,0x403bfd54u};
static const FxNativeTextProfile text_profile={(uintptr_t)fx_z63_current_page,0x40331b28u,0x4032b8bcu,0x404054a8u,0x404054e8u,0x40337910u,0x4032b3dcu,0x4dc0u,0x1c0u,0x118u,0x404056b0u,0x4032a814u,0x4032a6fcu};
static const FxNativeTitleProfile title_profile={(uintptr_t)fx_z63_current_page,0x40331b28u,0x40337910u,0x40405478u,0x40332accu,0x4033082cu,0x4dc6u};
static const FxNativeDialogStyle dialog_style={(const FxNativeDescriptor *)(uintptr_t)UINT64_C(0xa028c17c0),(const FxNativeRow *)(uintptr_t)UINT64_C(0x43b35004),340u,2u};
static const FxNativeUiReaderProfile reader_profile={{(uintptr_t)fx_z63_current_page,0x40331b28u,0x40337910u,0x40405478u,0x40332accu,0x4033082cu,0x4dc6u},0x4032a814u,0x4032a6fcu,720u,480u};
static const FxNativeEvent editor_events[]={
    {0x2b0u,0x4052f0a8u,0u,0u},{0x2b1u,0x4052f0acu,0u,0u},
    {0x2b2u,0x4052f0b0u,0u,0u},{0x2b3u,0x4052f0b4u,0u,0u},
    {0x2cau,0x4052f0c0u,0u,0u},{0x2cbu,0x4052f0c4u,0u,0u},
    {0x2ccu,0x4052f0c4u,0x403c2d14u,0u},{0x2cdu,0x4052f0c0u,0x403c2d14u,0u},
    {0x2dbu,0x4052f0fcu,0u,0u},{0x2deu,0x4052f100u,0u,0u},
    {0x334u,0x4052f108u,0u,0u},{0x336u,0x4052f10cu,0u,0u}};
static const FxNativeUiEditorProfile editor_profile={
    (const FxNativePage *)(uintptr_t)UINT64_C(0xa029140d0),editor_events,12u,0x2dcu,0x2abu,0x338u,
    0x4063cc9cu,0x4063f0b8u,0x40534e74u,0x403c3384u,(uintptr_t)fx_z63_current_page};
static FxResult prepare(FxNativeUi *u,FxNativePage *page) {
    const FxNativeDescriptor *d=(const FxNativeDescriptor *)page->descriptor;
    const FxNativeRow *rows=(const FxNativeRow *)d->rows;
    for(uint32_t i=0;i<d->count;++i) {
        uint32_t id=rows[i].word[6];int visible=fx_native_ui_row_visible(u,page->identity>>16,id),enabled=1;
        FxResult r=fx_native_row_visibility(&visibility,page,id,visible);if(r!=FX_OK)return r;
        uint32_t value=fx_native_ui_value(u,id,&enabled);
        r=fx_native_row_arrow(&visibility,page,id,visible && fx_native_ui_row_submenu(u,id));if(r!=FX_OK)return r;
        r=fx_native_row_value_visibility(&visibility,page,id,visible && value);if(r!=FX_OK)return r;
        r=fx_native_text_prepare(&text_profile,page,id,rows[i].word[0],value,enabled,u);if(r!=FX_OK)return r;
    }
    FxResult r=fx_native_ui_order_page(u,page);
    /* Type-8 buttons use the modal's fixed geometry. The ordinary list
     * selection repair cannot choose a row while both reader buttons are
     * disabled during its initial measured layout. */
    return r!=FX_OK || fx_native_page_modal(page)?r:fx_native_row_reflow(&layout,page);
}
static uint32_t opened(void *p) {
    FxNativePage *page=p;
    FxNativeUi *u=fx_native_ui_find_page(page->identity>>16);
    if(u && fx_native_page_modal(page)) {
        FxZ63Ui *s=fx_z63_menu_ui();Node *n=s?s->nodes:0;
        while(n && &n->ui!=u)n=n->next;
        const FxNativeDisplayProfile *d=&fx_native_display_profile;
        void *surface=((void *(*)(void *,uint32_t))d->native_global_surface)(0,(uint32_t)d->global_surface_index);
        if(n && !n->modal_owned && surface==(void *)page->surface) {
            memcpy(n->modal_target,(const uint8_t *)surface+0x108u,sizeof(n->modal_target));
            if(n->modal_target[0]==((uint32_t (*)(void))d->native_graphics_current_device)() && n->modal_target[1]==1u &&
               n->modal_target[2]==((uint32_t (*)(uint32_t))d->native_global_target)((uint32_t)d->global_surface_index)) {
                n->modal_original=((uint32_t (*)(uint32_t,uint32_t,uint32_t))d->native_graphics_target_enabled)
                    (n->modal_target[0],n->modal_target[1],n->modal_target[2]);
                if(!n->modal_original)((void (*)(uint32_t,uint32_t,uint32_t,uint32_t))d->native_graphics_target_enable)
                    (n->modal_target[0],n->modal_target[1],n->modal_target[2],1u);
                n->modal_owned=1u;
            }
        }
    }
    if(u && fx_native_page_modal(page) && fx_native_ui_page_matches(u,page)) {
        /* The C2.01 readback path can wait inside vgGetPixels before this
         * newly created foreground has submitted its first native frame.
         * Retain the normal focus/reference path and repaint owned native
         * ancestry instead of capturing an unpublished foreground. */
        fx_native_ui_capture_task();(void)fx_native_ui_open(u,page->identity>>16);
        fx_native_ui_refresh_owner(u->owner);
    } else (void)fx_native_ui_page_open(p);
    if(u)(void)prepare(u,page);return 1u;
}
static uint32_t closed(void *p) {
    FxNativePage *page=p;FxNativeUi *u=fx_native_ui_find_page(page->identity>>16);
    (void)fx_native_ui_page_close(p);
    if(u && fx_native_page_modal(page)) {
        FxZ63Ui *s=fx_z63_menu_ui();Node *n=s?s->nodes:0;while(n && &n->ui!=u)n=n->next;
        if(n && n->modal_owned) {
            if(fx_z63_current_page()==page && !memcmp((const uint8_t *)(uintptr_t)page->surface+0x108u,n->modal_target,sizeof(n->modal_target)))
                ((void (*)(uint32_t,uint32_t,uint32_t,uint32_t))fx_native_display_profile.native_graphics_target_enable)
                    (n->modal_target[0],n->modal_target[1],n->modal_target[2],n->modal_original);
            n->modal_owned=0u;
        }
    }
    return 1u;
}
FxResult fx_native_ui_flush_owner(FxPlatformOwner *owner) {
    void *mutex=(void *)(uintptr_t)UINT64_C(0xa03427f40);
    ((void (*)(void *))(uintptr_t)0x40d328b8u)(mutex);
    FxNativePage *page=fx_z63_current_page();FxNativeUi *u=page?fx_native_ui_find_page(page->identity>>16):0;
    FxResult r=FX_EMPTY;uint32_t destination=0u;
    if(u && u->owner==owner) {
        r=fx_native_ui_retirement_target(u,page->identity>>16,&destination);
        if(r==FX_EMPTY)r=fx_native_ui_prepare_navigation(u,page,&destination);
        if(r==FX_EMPTY) {
            uint32_t changes=0u;r=fx_native_ui_trace(u,1u,0u,fx_native_ui_recover_values(u,page->identity>>16,&changes));
            if(r==FX_OK && (changes&FX_NATIVE_UI_TITLE))r=fx_native_ui_trace(u,2u,0u,fx_native_title_prepare(&title_profile,page));
            if(r==FX_OK)r=fx_native_ui_trace(u,3u,0u,prepare(u,page));
            if(r==FX_OK)r=fx_native_ui_trace(u,4u,0u,fx_native_menu_restore(page));
            if(r==FX_OK) {
                ((void (*)(void *,uint32_t,uint32_t))(uintptr_t)0x40403ad0u)((void *)(uintptr_t)0x40403a6cu,3u,7u);
                r=fx_native_ui_presented(u,u->revision);
            }
        }
    }
    ((void (*)(void *))(uintptr_t)0x40d32864u)(mutex);
    if(destination){(void)fx_z63_menu_navigate(destination);fx_native_ui_refresh_owner(owner);}
    return r;
}
FxResult fx_native_apps_flush_native(void) {
    FxNativeApps *apps=fx_native_apps_domain();if(!apps)return FX_STATE;
    void *mutex=(void *)(uintptr_t)UINT64_C(0xa03427f40);
    ((void (*)(void *))(uintptr_t)0x40d328b8u)(mutex);
    uint32_t destination=0;FxResult r=fx_native_apps_prepare_navigation(apps,fx_z63_current_page(),&destination);
    ((void (*)(void *))(uintptr_t)0x40d32864u)(mutex);
    if(destination)(void)fx_z63_menu_navigate(destination);return r;
}
static uint32_t count(void *c){(void)c;return fx_z63_runtime_count(fx_z63_menu_runtime());}
static void pager(FxZ63Ui *s,FxNativeUi *u) {
    uint32_t n=0;int visible=0;
    if(!u->source && fx_ui_presence(u->owner,&n,&visible)==FX_OK && n>FX_UI_CONTROLS)
        (void)fx_native_ui_pager_enable(u,&s->space);
}
static FxNativeUi *instance(FxZ63Ui *s,uint32_t index,const FxNativePage *parent) {
    FxZ63Runtime *runtime=fx_z63_menu_runtime();FxPlatformOwner *owner=fx_z63_runtime_owner(runtime,index);if(!owner)return 0;
    FxNativeUi *cached=(void *)atomic_load_explicit(&owner->native_ui,memory_order_acquire);if(cached){
        uint32_t pinned=0;for(uint32_t i=0;i<FX_NATIVE_UI_PAGES;++i)pinned|=cached->pinned[i];
        if(!pinned){cached->profile.page=parent;cached->profile.back=parent->identity>>16;}
        pager(s,cached);return cached;
    }
    uint32_t slot=s->count;if(slot>=FX_NATIVE_UI_OWNERS || ensure(0,MODULE_TEXT+(slot+1u)*STRIDE)!=FX_OK)return 0;
    Node *node=allocate(sizeof(*node));if(!node)return 0;memset(node,0,sizeof(*node));
    FxNativeUi *u=&node->ui;FxNativeMenuProfile profile=s->profile;profile.page=parent;profile.back=parent->identity>>16;
    uintptr_t callbacks[5]={(uintptr_t)opened,(uintptr_t)closed,0x403c32c8u,(uintptr_t)noop,(uintptr_t)noop};
    uint32_t physical=0;
    if(fx_native_ui_space_take(&s->space,FX_NATIVE_UI_PAGES,&physical)!=FX_OK){map_release(0,node);return 0;}
    if(fx_native_ui_init(u,owner,&profile,allocate,physical,MODULE_TEXT+slot*STRIDE,0x8000u+slot*64u,
       fx_native_ui_actions,fx_native_ui_options,callbacks)!=FX_OK){fx_native_ui_space_rollback(&s->space,physical,FX_NATIVE_UI_PAGES);map_release(0,node);return 0;}
    u->request_refresh=fx_native_ui_refresh_owner;u->editor_profile=&editor_profile;u->dialog=&dialog_style;u->reader_profile=&reader_profile;
    if(fx_ui_dispatch_bind_page(&s->dispatch,u)!=FX_OK){fx_native_ui_space_rollback(&s->space,physical,FX_NATIVE_UI_PAGES);map_release(0,node);return 0;}
    fx_native_ui_identity(u,fx_z63_worker_identity(fx_z63_runtime_worker(runtime,index)));
    node->next=s->nodes;s->nodes=node;++s->count;
    atomic_store_explicit(&owner->native_ui,(uintptr_t)u,memory_order_release);pager(s,u);return u;
}
static FxResult entry(void *context,uint32_t index,const FxNativePage *parent,int selected,FxNativeAppEntry *out) {
    FxZ63Ui *s=context;FxPlatformOwner *owner=fx_z63_runtime_owner(fx_z63_menu_runtime(),index);if(!owner)return FX_EMPTY;
    if(owner->phase!=FX_OWNER_RUNNING && owner->phase!=FX_OWNER_FINISHED)return FX_EMPTY;
    uint32_t controls=0;int visible=0;FxResult r=fx_ui_presence(owner,&controls,&visible);if(r!=FX_OK)return r;
    if(!controls || !visible)return FX_EMPTY;
    if(!parent){memset(out,0,sizeof(*out));return FX_OK;}
    FxNativeUi *u=instance(s,index,parent);if(!u)return FX_BUDGET;
    if(selected && !fx_native_ui_page(u,u->page_base))return FX_BUDGET;
    *out=(FxNativeAppEntry){u->text_base+FX_NATIVE_UI_TEXTS,0u,u->page_base,1u,&u->app_links,u->observer};return FX_OK;
}
static FxResult action(void *c,uint32_t index){(void)c;return fx_z63_worker_action(fx_z63_runtime_worker(fx_z63_menu_runtime(),index),1u);}
uint32_t fx_z63_ui_manage(void *context) {
    (void)context;FxZ63Ui *s=fx_z63_menu_ui();if(!s)return 1u;
    if(!s->manager) {
        FxZ63Manager *m=allocate(sizeof(*m));if(!m)return 1u;
        uintptr_t callbacks[5]={(uintptr_t)opened,(uintptr_t)fx_native_ui_page_close,0x403c32c8u,(uintptr_t)noop,(uintptr_t)noop};
        if(ensure(0,MANAGER_TEXT+FX_NATIVE_UI_TEXTS+2u)!=FX_OK ||
           fx_z63_manager_init(m,&s->profile,allocate,MANAGER_TEXT,0x7d00u,callbacks,fx_z63_menu_runtime()!=0)!=FX_OK){map_release(0,m);return 1u;}
        m->native.manager.ui.capacity=&s->capacity;
        m->native.ui.request_refresh=fx_native_ui_refresh_owner;m->native.ui.editor_profile=&editor_profile;
        if(fx_ui_dispatch_register(&s->dispatch,&m->native.manager.ui)!=FX_OK ||
           fx_ui_dispatch_bind_page(&s->dispatch,&m->native.ui)!=FX_OK)return 1u;
        s->manager=m;
    }
    (void)fx_native_modules_open(&s->manager->native);return 1u;
}
FxResult fx_z63_ui_freeze(void) {
    FxZ63Ui *s=fx_z63_menu_ui();return s && s->manager?fx_module_manager_freeze(&s->manager->native.manager):FX_OK;
}
void fx_z63_ui_ready(void) {
    FxZ63Ui *s=fx_z63_menu_ui();if(s && s->manager){fx_module_manager_ready(&s->manager->native.manager);fx_native_ui_refresh_owner(&s->manager->native.manager.ui);}
}
void fx_z63_ui_shutdown(void) {
    FxZ63Ui *s=fx_z63_menu_ui();if(s && s->manager)fx_module_manager_shutdown(&s->manager->native.manager);
}
FxZ63Ui *fx_z63_ui_create(const FxNativeMenuProfile *profile) {
    FxZ63Ui *s=allocate(sizeof(*s));if(!s)return 0;memset(s,0,sizeof(*s));s->profile=*profile;
    if(fx_native_ui_space_init(&s->space,&s->dispatch,(FxIndexMemory){0,map_allocate,map_release},0,ensure,
       FX_NATIVE_MODULE_PAGE_BASE,FX_NATIVE_EXTENSION_PAGE_LIMIT,MODULE_TEXT,STRIDE,MODULE_TEXT+FX_NATIVE_UI_OWNERS*STRIDE)!=FX_OK)return 0;
    if(fx_platform_capacity_init(&s->capacity,&s->domains,(FxIndexMemory){0,map_allocate,map_release},(FxAllocationObserver){0},&s->space)!=FX_OK)return 0;
    uintptr_t callbacks[5]={(uintptr_t)noop,(uintptr_t)noop,0x403c32c8u,(uintptr_t)noop,(uintptr_t)noop};
    if(fx_native_apps_init(&s->apps,profile,callbacks,s,allocate,count,entry,action,TEXT,TEXT+1u,TEXT+2u,TEXT+3u,TEXT+4u,TEXT+5u,TEXT+6u)!=FX_OK)return 0;
    s->apps.capture_task=fx_native_ui_capture_task;s->apps.embedded=1u;s->dispatch.tasks.owner_account=fx_account_task_storage;return s;
}
FxNativePage *fx_z63_ui_page(uint32_t page) {
    FxZ63Ui *s=fx_z63_menu_ui();if(!s)return 0;
    if(fx_native_apps_is_page(page))return fx_native_apps_page(&s->apps,page);
    FxNativeUi *u=fx_ui_dispatch_find_page(&s->dispatch,page);return u?fx_native_ui_page(u,page):0;
}
int fx_z63_ui_text(uint32_t id,const uint16_t **text,uint32_t *bytes) {
    FxZ63Ui *s=fx_z63_menu_ui();if(!s)return 0;
    static const uint16_t *const labels[]={u"Applications",u"Previous",u"Next",u"No applications",u"Back",u"Unavailable",u"Retry",u"Manage modules"};
    if(id>=TEXT && id<TEXT+8u){*text=labels[id-TEXT];uint32_t n=0;while((*text)[n])++n;*bytes=2u*n;return 1;}
    if(s->manager && fx_native_ui_text(&s->manager->native.ui,id,text,bytes))return 1;
    if(fx_native_ui_pager_text(&s->space,id,text,bytes))return 1;
    for(Node *n=s->nodes;n;n=n->next) {
        FxNativeUi *u=&n->ui;if(fx_native_ui_text(u,id,text,bytes))return 1;
        if(id==u->text_base+FX_NATIVE_UI_TEXTS){*text=u->contribution_title;uint32_t k=0;while(k<FX_UI_TEXT && (*text)[k])++k;*bytes=k*2u;return 1;}
    }
    return 0;
}
