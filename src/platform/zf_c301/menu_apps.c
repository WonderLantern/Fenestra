/* Z9 Apps and module view construction with the Zf native menu profile.
 * Called only by the original GUI task; all published storage is retained. */
#include "menu_model.h"
#include "owner_publish.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_platform_native.h"
#include "fenestra_memory.h"
#include "fenestra_accounting_service.h"
#include "special_ui_profile.h"

static void *allocate(uint32_t bytes) {
    return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d31898u)(bytes,0u);
}
static void *map_allocate(void *context,uint32_t bytes){(void)context;return allocate(bytes);}
static void map_release(void *context,void *memory) {
    (void)context;((void (*)(void *))(uintptr_t)0x40d318d8u)(memory);
}
static uint32_t noop(void *p){(void)p;return 1u;}
static uint32_t opened(void *p){return fx_native_ui_page_open(p);}

FxResult fx_zf_menu_ensure_text(FxZfMenu *menu,uint32_t required) {
    if(!menu || !menu->bank)return FX_STATE;
    FxNativeTextBank *bank=0;
    FxResult result=fx_native_text_bank_grow((FxIndexMemory){0,map_allocate,map_release},
                                            menu->bank,required,&bank);
    if(result!=FX_OK || bank==menu->bank)return result;
    *(uintptr_t volatile *)(uintptr_t)UINT64_C(0xa0304f6a0)=(uintptr_t)bank->fonts;
    *(uintptr_t volatile *)(uintptr_t)UINT64_C(0xa0304f6b0)=(uintptr_t)bank->attributes;
    /* GUI exclusion serializes the same roots as the Z9 adapter. Old banks
     * remain linked/retained by fx_native_text_bank_grow. */
    atomic_thread_fence(memory_order_release);menu->bank=bank;return FX_OK;
}
static FxResult ensure_text(void *context,uint32_t required) {
    return fx_zf_menu_ensure_text(context,required);
}
FxResult fx_zf_menu_resources_prepare(FxZfMenu *menu,FxZfOwnerStorage *owner) {
    if(!menu || !owner)return FX_ARGUMENT;
    FxIndexMemory memory={0,map_allocate,map_release};
    FxResult result=fx_native_ui_space_init(&menu->ui_space,&owner->ui_dispatch,memory,
        menu,ensure_text,FX_NATIVE_MODULE_PAGE_BASE,FX_NATIVE_EXTENSION_PAGE_LIMIT,
        FX_ZF_MODULE_TEXT_BASE,FX_ZF_MODULE_TEXT_STRIDE,
        FX_ZF_MODULE_TEXT_BASE+FX_NATIVE_UI_OWNERS*FX_ZF_MODULE_TEXT_STRIDE);
    if(result!=FX_OK)return result;
    if(!atomic_load_explicit(&owner->accounting.ready,memory_order_acquire)) {
        result=fx_shared_accounts_init(&owner->accounting,memory,(uint32_t)sizeof(*owner),
            (uint32_t)sizeof(owner->ui_dispatch.tasks));
        if(result!=FX_OK)return result;
        owner->hooks.page_observer=fx_account_observer(&owner->accounting.pages);
        owner->hooks.owner_account=fx_account_hook_storage;
        owner->ui_dispatch.tasks.owner_account=fx_account_task_storage;
    }
    if(!atomic_load_explicit(&owner->capacity_registry.ready,memory_order_acquire)) {
        result=fx_platform_capacity_init(&owner->capacity_registry,&owner->capacity_domains,memory,
            fx_account_observer(fx_shared_account_row(&owner->accounting.state,"capacity.metadata")),
            &menu->ui_space);
        if(result!=FX_OK)return result;
    }
    for(FxNativeTextBank *bank=menu->bank;bank;bank=(FxNativeTextBank *)bank->previous)
        fx_retained_account(fx_shared_text_account(&owner->accounting),&bank->accounted,bank->bytes);
    if(menu->modules_manager) {
        fx_retained_account(fx_shared_ui_account(&owner->accounting),&menu->manager_accounted,
            (uint32_t)sizeof(*menu->modules_manager));
        result=fx_module_manager_account(&menu->modules_manager->manager,
            fx_account_observer(&owner->accounting.indexes),fx_account_observer(&owner->accounting.inspect));
    }
    return result;
}
static uint32_t count(void *context) {
    (void)context;FxZfLoadedModules *registry=fx_zf_owner_modules();uint32_t n=0;
    for(FxZfLoadedModule *r=registry?atomic_load_explicit(&registry->head,memory_order_acquire):0;r;r=r->next)++n;
    return n;
}
static FxZfLoadedModule *record(uint32_t index) {
    FxZfLoadedModules *registry=fx_zf_owner_modules();
    FxZfLoadedModule *head=registry?atomic_load_explicit(&registry->head,memory_order_acquire):0;
    uint32_t n=0;for(FxZfLoadedModule *r=head;r;r=r->next)++n;
    if(index>=n)return 0;
    for(uint32_t skip=n-index-1u;skip;--skip)head=head->next;
    return head;
}
static FxNativeUi *instance_ui(FxZfMenu *menu,FxZfLoadedModule *r,uint32_t index,const FxNativePage *parent) {
    FxPlatformOwner *owner=r->owner;
    FxNativeUi *ui=(void *)atomic_load_explicit(&owner->native_ui,memory_order_acquire);
    if(ui) {
        uint32_t pinned=0;for(uint32_t i=0;i<FX_NATIVE_UI_PAGES;++i)pinned|=ui->pinned[i];
        if(!pinned){ui->profile.page=parent;ui->profile.back=parent->identity>>16;}
        return ui;
    }
    if(index>=FX_NATIVE_UI_OWNERS)return 0;
    uint32_t texts=FX_ZF_MODULE_TEXT_BASE+index*FX_ZF_MODULE_TEXT_STRIDE;
    if(fx_zf_menu_ensure_text(menu,texts+FX_ZF_MODULE_TEXT_STRIDE)!=FX_OK)return 0;
    FxNativeUiDispatch *dispatch=fx_native_ui_dispatch_domain();
    if(fx_native_ui_space_init(&menu->ui_space,dispatch,(FxIndexMemory){0,map_allocate,map_release},
        menu,ensure_text,FX_NATIVE_MODULE_PAGE_BASE,FX_NATIVE_EXTENSION_PAGE_LIMIT,
        FX_ZF_MODULE_TEXT_BASE,FX_ZF_MODULE_TEXT_STRIDE,
        FX_ZF_MODULE_TEXT_BASE+FX_NATIVE_UI_OWNERS*FX_ZF_MODULE_TEXT_STRIDE)!=FX_OK)return 0;
    ui=allocate((uint32_t)sizeof(*ui));if(!ui)return 0;memset(ui,0,sizeof(*ui));
    uint32_t controls=0,physical=0,pages=FX_NATIVE_UI_PAGES;int visible=0;
    if(fx_ui_presence(owner,&controls,&visible)==FX_OK && controls>FX_UI_CONTROLS)pages=2u;
    if(fx_native_ui_space_take(&menu->ui_space,pages,&physical)!=FX_OK){map_release(0,ui);return 0;}
    FxNativeMenuProfile profile=menu->profile;profile.page=parent;profile.back=parent->identity>>16;
    uintptr_t callbacks[5]={(uintptr_t)opened,(uintptr_t)fx_native_ui_page_close,
        (uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop};
    FxResult result=fx_native_ui_init(ui,owner,&profile,allocate,physical,texts,
        0x8000u+index*64u,fx_native_ui_actions,fx_native_ui_options,callbacks);
    if(result!=FX_OK){fx_native_ui_space_rollback(&menu->ui_space,physical,pages);map_release(0,ui);return 0;}
    if(pages==2u)ui->picker_page=physical+1u;
    ui->dialog=&fx_zf_ui_dialog;
    ui->editor_profile=&fx_zf_ui_editor_profile;
    ui->reader_profile=&fx_zf_ui_reader_profile;
    ui->request_refresh=fx_native_ui_refresh_owner;
    if(fx_ui_dispatch_bind_page(dispatch,ui)!=FX_OK) {
        fx_native_ui_space_rollback(&menu->ui_space,physical,pages);map_release(0,ui);return 0;
    }
    fx_native_ui_identity(ui,r->id);
    atomic_store_explicit(&owner->native_ui,(uintptr_t)ui,memory_order_release);
    if(controls>FX_UI_CONTROLS)(void)fx_native_ui_pager_enable(ui,&menu->ui_space);
    return ui;
}
static FxResult entry(void *context,uint32_t index,const FxNativePage *parent,int selected,FxNativeAppEntry *out) {
    FxZfMenu *menu=context;FxZfLoadedModule *r=record(index);if(!r || !r->owner)return FX_EMPTY;
    FxPlatformOwner *owner=r->owner;uint32_t phase=atomic_load_explicit(&owner->phase,memory_order_acquire);
    if(phase!=FX_OWNER_RUNNING && phase!=FX_OWNER_FINISHED)return FX_EMPTY;
    uint32_t controls=0;int visible=0;FxResult result=fx_ui_presence(owner,&controls,&visible);
    if(result!=FX_OK)return result;
    if(!controls || !visible)return FX_EMPTY;
    if(!parent){memset(out,0,sizeof(*out));return FX_OK;}
    FxNativeUi *ui=instance_ui(menu,r,index,parent);if(!ui)return FX_BUDGET;
    if(selected && !fx_native_ui_page(ui,ui->page_base))return FX_BUDGET;
    if(selected && !fx_native_ui_root_visible(ui))return FX_EMPTY;
    *out=(FxNativeAppEntry){ui->text_base+FX_NATIVE_UI_TEXTS,0u,ui->page_base,1u,&ui->app_links,ui->observer};
    return FX_OK;
}
static FxResult action(void *context,uint32_t index) {
    (void)context;FxZfLoadedModule *r=record(index);
    return r && r->owner?fx_zf_owner_module_action(r->owner,1u):FX_EMPTY;
}
FxNativePage *fx_zf_menu_apps_page(FxZfMenu *menu,uint32_t page) {
    if(!menu || !fx_native_apps_is_page(page))return 0;
    FxNativeApps *apps=&menu->apps;
    if(!apps->allocate) {
        uintptr_t callbacks[5]={(uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop};
        if(fx_native_apps_init(apps,&menu->profile,callbacks,menu,allocate,count,entry,action,
            2899u,2919u,2920u,2903u,2902u,2922u,2923u)!=FX_OK)return 0;
        apps->capture_task=fx_native_ui_capture_task;apps->embedded=1u;
    }
    if(fx_zf_apps_publish(fx_zf_owner_apps_domain(),apps)!=FX_OK)return 0;
    return fx_native_apps_page(apps,page);
}
int fx_zf_menu_module_text(FxZfMenu *menu,uint32_t id,const uint16_t **text,uint32_t *bytes) {
    if(!menu || !text || !bytes)return 0;
    if(fx_native_ui_pager_text(&menu->ui_space,id,text,bytes))return 1;
    if(id<FX_ZF_MODULE_TEXT_BASE)return 0;
    static const uint16_t empty[]=u"";*text=empty;*bytes=0;
    uint32_t index=(id-FX_ZF_MODULE_TEXT_BASE)/FX_ZF_MODULE_TEXT_STRIDE;
    uint32_t offset=(id-FX_ZF_MODULE_TEXT_BASE)%FX_ZF_MODULE_TEXT_STRIDE;
    FxZfLoadedModule *r=record(index);
    FxNativeUi *ui=r?(void *)atomic_load_explicit(&r->owner->native_ui,memory_order_acquire):0;
    if(!ui)return 1;
    if(offset<FX_NATIVE_UI_TEXTS){(void)fx_native_ui_text(ui,id,text,bytes);return 1;}
    if(offset==FX_NATIVE_UI_TEXTS) {
        *text=ui->contribution_title;uint32_t n=0;while(n<FX_UI_TEXT && (*text)[n])++n;*bytes=n*2u;
    }
    return 1;
}
