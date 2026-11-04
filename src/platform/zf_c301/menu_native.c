#if !defined(FX_ZF_EXPERIMENTAL_MENU_NATIVE) || FX_ZF_EXPERIMENTAL_MENU_NATIVE != 1
#error "Zf menu publication requires the native-menu feature macro"
#endif
#include "menu_model.h"
#include "owner_publish.h"
#include "fenestra_native_namespace.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_platform_native.h"
#include "fx_zf_dispatch_profile.h"
#include "fenestra_memory.h"
#include "special_ui_profile.h"
extern void fx_zf_menu_text_original(int32_t,int32_t,const uint16_t **,uint32_t *);
extern uint64_t fx_zf_menu_navigate_original(uint32_t);
static FxZfOwnerStorage *owner(void) {
    FxZfLifecycle *life=fx_zf_runtime_lifecycle();
    return life?(void *)((uint8_t *)life - offsetof(FxZfOwnerStorage,lifecycle)):0;
}
static FxZfMenu *state(void) {
    FxZfOwnerStorage *s=owner();if(!s)return 0;
    FxZfMenu *m=(void *)atomic_load_explicit(&s->menu,memory_order_acquire);
    return m && atomic_load_explicit(&m->ready,memory_order_acquire)?m:0;
}
static void *allocate(void *context,uint32_t bytes) {
    (void)context;return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d31898u)(bytes,0u);
}
static void release(void *context,void *p) {
    (void)context;((void (*)(void *))(uintptr_t)0x40d318d8u)(p);
}
static uint32_t noop(void *p){(void)p;return 1u;}
static void *allocate_manager(uint32_t bytes) {
    return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d31898u)(bytes,0u);
}
static uint32_t prepare_manager(void *);
static uint32_t activate(void *p) {
    FxZfMenu *s=state();if(!s || !prepare_manager(p))return 0u;
    if(fx_zf_menu_resources_prepare(s,owner())!=FX_OK)return 0u;
    FxZfOwnerStorage *o=owner();
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
    if(!o || !atomic_load_explicit(&o->startup_ready,memory_order_acquire))return 0u;
    return o && fx_startup_service_request(&o->startup,FX_START_ACTIVATE)==FX_OK;
#else
    (void)o;atomic_store_explicit(&s->activation_requested,1u,memory_order_release);return 1u;
#endif
}
static uint32_t auto_set(void *p,uint32_t request) {
    (void)p;FxZfOwnerStorage *o=owner();if(!o)return 0u;
    if(!atomic_load_explicit(&o->startup_ready,memory_order_acquire))return 0u;
    return fx_startup_service_request(&o->startup,request)==FX_OK;
}
static uint32_t auto_on(void *p){return auto_set(p,FX_START_AUTO_ON);}
static uint32_t auto_off(void *p){return auto_set(p,FX_START_AUTO_OFF);}
static uint32_t clear_guard(void *p) {
    (void)p;FxZfOwnerStorage *o=owner();
    if(!o || !atomic_load_explicit(&o->startup_ready,memory_order_acquire))return 0u;
    return o && fx_startup_service_request(&o->startup,FX_START_CLEAR)==FX_OK;
}
static uint32_t usb_toggle(void *p) {
    (void)p;FxResult result=fx_zf_ptp_set_user_enabled(!fx_zf_ptp_user_enabled());
    if(result==FX_OK)fx_zf_menu_refresh_core();
    return result==FX_OK;
}
static uint32_t prepare_manager(void *p) {
    (void)p;
    FxZfMenu *s=state();
    if(!s)return 0u;
    if(!s->modules_manager) {
        FxNativeModules *m=allocate_manager((uint32_t)sizeof(*m));
        if(!m)return 0u;
        if(fx_zf_menu_ensure_text(s,FX_ZF_MODULE_TEXT_BASE)!=FX_OK){release(0,m);return 0u;}
        uintptr_t callbacks[5]={(uintptr_t)fx_native_ui_page_open,
            (uintptr_t)fx_native_ui_page_close,(uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop};
        FxNativeMenuProfile profile=s->profile;profile.page=&s->core.page;profile.back=FX_NATIVE_FENESTRA_PAGE;
        FxResult initialized=fx_native_modules_init(m,fx_platform_target(),&profile,allocate_manager,
                                  FX_ZF_MANAGER_TEXT_BASE,0x7d00u,callbacks,0);
        if(initialized!=FX_OK) {
            release(0,m);
            return 0u;
        }
        m->ui.editor_profile=&fx_zf_ui_editor_profile;
        /* The manager owns a second native view.  Publish its owner and page
         * span before any navigation can resolve the first manager page. */
        FxNativeUiDispatch *dispatch=fx_native_ui_dispatch_domain();
        if(!dispatch) {
            release(0,m);
            return 0u;
        }
        FxResult registered=fx_ui_dispatch_register(dispatch,&m->manager.ui);
        if(registered!=FX_OK) {
            release(0,m);
            return 0u;
        }
        FxResult bound=fx_ui_dispatch_bind_page(dispatch,&m->ui);
        if(bound!=FX_OK) {
            /* Dispatch registration may retain the manager; keep it allocated. */
            return 0u;
        }
        s->modules_manager=m;
        FxZfLifecycle *life=fx_zf_runtime_lifecycle();
        if(life) {
            FxZfOwnerStorage *owner=(void *)((uint8_t *)life-
                offsetof(FxZfOwnerStorage,lifecycle));
            owner->modules_manager=m;
        }
    }
    fx_native_ui_refresh_owner(&s->modules_manager->manager.ui);
    return 1u;
}
static uint32_t open_manager(void *p) {
    if(!prepare_manager(p))return 0u;
    FxZfMenu *s=state();
    (void)fx_native_modules_open(s->modules_manager);
    return 1u;
}
static uint32_t opened(void *p) {
    FxZfMenu *s=state();if(!s)return 0u;
    if(p!=&s->core.page && p!=&s->wonder.page && p!=&s->auto_menu.page && p!=&s->guard_menu.page)return 0u;
    fx_native_ui_capture_task();atomic_fetch_add(&s->opened,1u);
    extern uint32_t fx_zf_menu_opened_native(FxNativePage *);
    return fx_zf_menu_opened_native(p);
}
FxResult fx_zf_menu_install(void) {
    FxZfOwnerStorage *o=owner();if(!o)return FX_STATE;
    /* Startup dispatcher, before admitting new pages; use Nikon's checked GUI
     * exclusion for root replacement. Only the explicit QEMU menu option calls
     * this. Never free storage after publication, including partial failures. */
    void *guard=(void *)(uintptr_t)FX_ZF_GUI_OBJECT;
    ((void (*)(void *))(uintptr_t)FX_ZF_GUI_ENTER)(guard);
    FxResult result=FX_STATE;
    if(state())goto prepare;
    if(atomic_load_explicit(&o->menu,memory_order_acquire))goto done;
    const uint8_t *fonts=*(const uint8_t *volatile *)(uintptr_t)UINT64_C(0xa0304f6a0);
    const uintptr_t *attrs=*(const uintptr_t *volatile *)(uintptr_t)UINT64_C(0xa0304f6b0);
    if(fonts!=(const void *)(uintptr_t)0x4314d00du || attrs!=(const void *)(uintptr_t)0x437d8778u)goto done;
    FxZfMenu *m=allocate(0,(uint32_t)sizeof(*m));if(!m){result=FX_BUDGET;goto done;}
    memset(m,0,sizeof(*m));
    FxNativePage source=*(const FxNativePage *)o->page;source.descriptor=(uintptr_t)&o->stock;
    uintptr_t callbacks[5]={(uintptr_t)opened,(uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop};
    const FxZfMenuActions actions={(uintptr_t)activate,(uintptr_t)open_manager,(uintptr_t)usb_toggle,
        (uintptr_t)auto_on,(uintptr_t)auto_off,(uintptr_t)clear_guard};
    result=fx_zf_menu_prepare(m,(const FxNativePage *)&source,
        (const void *)(uintptr_t)0x439db818u,callbacks,&actions);
    if(result!=FX_OK){release(0,m);goto done;}
    /* The inherited previous pointer remains stock. The source page copy was
     * construction-only and is never retained by these parentless pages. */
    const FxIndexMemory memory={0,allocate,release};
    result=fx_native_text_bank_create(memory,fonts,attrs,2898u,29u,FX_ZF_MODULE_TEXT_BASE,fonts[866],
        ((const uint8_t *)attrs[0])[618],&m->bank);
    if(result!=FX_OK){release(0,m);goto done;}
    atomic_store_explicit(&o->menu,(uintptr_t)m,memory_order_release);
    *(uintptr_t volatile *)(uintptr_t)UINT64_C(0xa0304f6a0)=(uintptr_t)m->bank->fonts;
    *(uintptr_t volatile *)(uintptr_t)UINT64_C(0xa0304f6b0)=(uintptr_t)m->bank->attributes;
    atomic_store_explicit(&m->ready,1u,memory_order_release);result=FX_OK;
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
prepare:
    /* Prepare the retained manager/UI space under native GUI exclusion. The
     * owner may later activate automatically without editing GUI roots. */
    if(!prepare_manager(0)){result=FX_BUDGET;goto done;}
    result=fx_zf_menu_resources_prepare(state(),o);
    if(result==FX_OK && !atomic_load_explicit(&o->startup_ready,memory_order_acquire))
        result=fx_zf_startup_prepare(o);
#else
prepare:
    result=FX_OK;
#endif
done:
    ((void (*)(void *))(uintptr_t)FX_ZF_GUI_LEAVE)(guard);return result;
}
FxNativePage *fx_zf_menu_page(uint32_t group,uint32_t id) {
    id &= 65535u;
    FxZfMenu *s=state();
    if(group==2u && s) {
        atomic_fetch_add(&s->lookups,1u);
        if(s->modules_manager && id>=FX_NATIVE_MANAGER_PAGE_BASE &&
           id<FX_NATIVE_MANAGER_PAGE_BASE+FX_NATIVE_UI_PAGES)
            return fx_native_ui_page(&s->modules_manager->ui,id);
        FxNativeUi *ui=fx_ui_dispatch_find_page(fx_native_ui_dispatch_domain(),id);
        if(ui)return fx_native_ui_page(ui,id);
        if(id==672u)return &s->setup_page;
        if(id==FX_NATIVE_FENESTRA_PAGE)return &s->core.page;
        if(id==FX_NATIVE_STARTUP_AUTO_PAGE) {
            FxZfOwnerStorage *o=owner();
            s->auto_menu.page.selected=(uint16_t)(o && (atomic_load(&o->startup.desired)&FX_STARTUP_AUTO)?0u:1u);
            return &s->auto_menu.page;
        }
        if(id==FX_NATIVE_STARTUP_GUARD_PAGE){s->guard_menu.page.selected=0;return &s->guard_menu.page;}
        if(fx_native_apps_is_page(id))return fx_zf_menu_apps_page(s,id);
    }
    if(group==2u && id>=FX_NATIVE_FENESTRA_PAGE && id<FX_NATIVE_EXTENSION_PAGE_LIMIT)return 0;
    if(group>6u)return 0;
    uintptr_t base=((const volatile uintptr_t *)(uintptr_t)UINT64_C(0xa03401b88))[group];
    return base?(void *)(base+sizeof(FxNativePage)*id):0;
}
void fx_zf_menu_text(int32_t language,int32_t id,const uint16_t **text,uint32_t *bytes) {
    FxZfMenu *s=state();
    FxZfOwnerStorage *o=owner();
    if(o && language>=0 && language<29 && id==2908) {
        const uint16_t *value=0;uint32_t size=0;
#define STATUS(v) do {static const uint16_t msg[]=u##v;value=msg;size=(uint32_t)sizeof(msg)-2u;} while(0)
        switch(atomic_load(&o->startup.phase)) {
            case FX_STARTUP_READY:STATUS("Ready");break;
            case FX_STARTUP_ABNORMAL:STATUS("Disabled: abnormal shutdown");break;
            case FX_STARTUP_IO_ERROR:STATUS("Card startup I/O error");break;
            case FX_STARTUP_ARMED:STATUS("Loading modules");break;
            case FX_STARTUP_RUNNING:STATUS("Active");break;
            case FX_STARTUP_FAULT:STATUS("Module fault");break;
            default:STATUS("Checking startup");break;
        }
#undef STATUS
        *text=value;*bytes=size;return;
    }
    if(s && id>=(int32_t)FX_ZF_MODULE_TEXT_BASE &&
       fx_zf_menu_module_text(s,(uint32_t)id,text,bytes))return;
    if(s && s->modules_manager && language>=0 && language<29 &&
       id>=(int32_t)FX_ZF_MANAGER_TEXT_BASE && id<(int32_t)FX_ZF_MODULE_TEXT_BASE &&
       fx_native_ui_text(&s->modules_manager->ui,(uint32_t)id,text,bytes))return;
    if(s && language>=0 && language<29 && fx_zf_menu_label(id,text,bytes)) {
        atomic_fetch_add(&s->texts,1u);return;
    }
    fx_zf_menu_text_original(language,id,text,bytes);
}
uint64_t fx_zf_menu_navigate(uint32_t page) {
    if(page>=FX_NATIVE_FENESTRA_PAGE && page<FX_NATIVE_EXTENSION_PAGE_LIMIT) {
        if(!state())return 0;
        return ((uint64_t (*)(uint32_t,uint32_t))(uintptr_t)0x403b6250u)(2u,page);
    }
    return fx_zf_menu_navigate_original(page);
}
