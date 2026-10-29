#include "fenestra_native_ui_editor.h"
extern const uint8_t __fx_core_code_begin[],__fx_core_code_end[];
#include "fenestra_native_ui_reader.h"
#include "fenestra_native_font.h"
#include "fenestra_native_apps.h"
/* C5.32 native front end. All persistent code/data is immutable; ownership
 * lives in ordinary Nikon heap RAM. No ZR addresses or startup event IDs. */
#include "fenestra_memory.h"
#include "fenestra_native_menu.h"
#include "fenestra_native_text.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_ui_pager.h"
#include "fenestra_native_visibility.h"
#include "fenestra_native_title.h"
#include "fenestra_platform_native.h"
#include "fenestra_native_modules.h"
#include "fenestra_module_bus.h"
#include "fenestra_hooks.h"
#include "fenestra_native_mapping.h"
#include "module_worker.h"
#include "menu_data.h"
#include "services.h"
#include "fenestra_accounting_service.h"
#include "fenestra_shared_accounts.h"
#include "fenestra_native_namespace.h"
#include "fenestra_load_sequence.h"
#include "fenestra_startup_service.h"
#include "fenestra_startup_files.h"
#include "ptp.h"
#include "fenestra_native_rtos.h"
#include "fenestra_native_linux.h"
#include "fenestra_native_panic.h"
#define FN(a, t) ((t)(uintptr_t)UINT64_C(a))
#define SETUP 638u
#define FENESTRA FX_NATIVE_FENESTRA_PAGE
#define WONDER FX_NATIVE_WONDER_PAGE
#define TEXT 3204u
#define UI_PAGE_BASE FX_NATIVE_MODULE_PAGE_BASE
#define UI_TEXT_BASE (TEXT + 26u)
#define MODULE_TEXT_BASE (UI_TEXT_BASE+FX_NATIVE_UI_TEXTS)
#define MODULE_TEXT_STRIDE (FX_NATIVE_UI_TEXTS+2u)
#define TEXT_COUNT (MODULE_TEXT_BASE-TEXT)
#define LANGUAGES 29u
#define MAGIC UINT32_C(0x465a394d)
typedef FxNativePanicPage PanicPage;
typedef struct {
    FxNativeDescriptor setup;
    uint32_t magic, installed;
    _Atomic uint32_t activation, phase, error, shutdown, retry;
    FxNativeRow rows[47];
    uintptr_t events[47];
    FxNativeEvent entry_events[2][18];
    FxNativeMenu core, wonder;
    _Atomic(FxNativeTextBank *) text_bank;
    FxZ9TaskScope task;
    _Atomic(void *) task_stack;
    uint8_t scratch[16385];
    FxZ9ModuleWorker **workers;
    FxZ9Services **services;uint32_t capacity;
    FxNativeUi **ui;uint32_t ui_count,ui_capacity;
    _Atomic(FxNativeApps *) apps;
    _Atomic(FxNativeModules *) management;
    _Atomic uint32_t management_accounted,ui_vector_accounted;
    _Atomic uint32_t stack_accounted,panic_accounted;
    _Atomic(FxModuleBus *) bus;
    FxHookRegistry hooks;
    FxNativeMappingRegistry mappings;
    FxNativeUiDispatch ui_dispatch;FxNativeUiSpace ui_space;

    _Atomic uint32_t modules;
    uint32_t skipped;
    FxStartupService startup;
    FxStartupFiles startup_files;
    FxNativeMenu auto_menu,guard_menu;
    FxPtp ptp;
    FxRtos rtos;
    FxLinuxBridge linux;
    PanicPage *panic;
    _Atomic uint64_t fault_record; /* owner/error published together */
    FxNativeFont font_scope;
    FxTextRegions text_regions;FxTextRegion state_text_region,image_text_region;
    FxSharedAccounts accounting;
    FxCapacityRegistry capacity_registry;FxNativeCapacityDomains capacity_domains;
    FxNativeApps apps_storage;

} State;
static FxNativePage *stock(uint32_t group, uint32_t id) {
    if (group > 6u)
        return 0;
    uintptr_t root = ((volatile uintptr_t *)(uintptr_t)UINT64_C(0xa032d5de8))[group];
    return root ? (FxNativePage *)(root + (id & 65535u) * sizeof(FxNativePage)) : 0;
}
static void *map_allocate(void *context,uint32_t bytes){(void)context;return FN(0x40d30458,void *(*)(uint32_t,uint32_t))(bytes,0u);}
static void map_release(void *context,void *memory){(void)context;FN(0x40d30498,void (*)(void *))(memory);}
static int text_ensure(State *s,uint32_t required) {
    FxNativeTextBank *old=atomic_load_explicit(&s->text_bank,memory_order_acquire),*bank=0;
    if(fx_native_text_bank_grow((FxIndexMemory){0,map_allocate,map_release},old,required,&bank)!=FX_OK)return 0;
    if(bank==old)return 1;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa02f4ab30)=(uintptr_t)bank->fonts;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa02f4ab40)=(uintptr_t)bank->attributes;
    __asm__ volatile("dmb ish":::"memory");
    atomic_store_explicit(&s->text_bank,bank,memory_order_seq_cst);
    fx_retained_account(fx_shared_text_account(&s->accounting),&bank->accounted,bank->bytes);return 1;
}
static State *state(void) {
    FxNativePage *p = stock(2u, SETUP);
    if (!p || p->descriptor == UINT64_C(0xa028d1a10))
        return 0;
    uintptr_t a = p->descriptor;
    if ((a & 7u) || a < UINT64_C(0xa00000000) || a > UINT64_C(0xa40000000) - sizeof(State))
        return 0;
    State *s = (State *)a;
    return s->magic == MAGIC && s->installed ? s : 0;
}
static void *allocate(uint32_t n) { return FN(0x40d30458, void *(*)(uint32_t, uint32_t))(n, 0u); }
FxPtp *fx_z9_menu_ptp(void) {State *s=state();return s?&s->ptp:0;}
FxRtos *fx_z9_menu_rtos(void) {State *s=state();return s?&s->rtos:0;}
FxLinuxBridge *fx_z9_menu_linux(void) {State *s=state();return s?&s->linux:0;}
int fx_z9_menu_ptp_active(void) {
    State *s=state();return s && atomic_load(&s->activation) &&
        !atomic_load(&s->shutdown) && !atomic_load(&s->startup.shutdown);
}
void fx_native_module_fault(FxPlatformOwner *owner,FxResult error) {
    State *s=state();if(!s)return;
    if(error==FX_OK)error=FX_INTEGRITY;
    uint64_t zero=0u,record=((uint64_t)(owner?owner->owner:0u)<<32)|(uint32_t)error;
    (void)atomic_compare_exchange_strong(&s->fault_record,&zero,record);
    record=atomic_load_explicit(&s->fault_record,memory_order_acquire);
    atomic_store(&s->error,(uint32_t)record);
    atomic_store_explicit(&s->phase,5u,memory_order_release);atomic_store_explicit(&s->shutdown,1u,memory_order_release);
}
FxHookRegistry *fx_native_hooks(void) {State *s=state();return s?&s->hooks:0;}
FxAccountState *fx_native_shared_accounts(void){State *s=state();return s && atomic_load_explicit(&s->accounting.ready,memory_order_acquire)?&s->accounting.state:0;}
FxCapacityRegistry *fx_native_capacity_registry(void){State *s=state();return s && atomic_load_explicit(&s->capacity_registry.ready,memory_order_acquire)?&s->capacity_registry:0;}
FxNativeMappingRegistry *fx_native_mappings(void) {State *s=state();return s?&s->mappings:0;}
FxModuleBus *fx_native_module_bus(void) {
    State *s=state();if(!s)return 0;
    FxModuleBus *b=atomic_load_explicit(&s->bus,memory_order_acquire);if(b)return b;
    b=allocate((uint32_t)sizeof(*b));if(!b)return 0;memset(b,0,sizeof(*b));
    if(atomic_load_explicit(&s->accounting.ready,memory_order_acquire))b->metadata=fx_account_observer(&s->accounting.bus);
    b->owner_queue=fx_account_bus_queues;fx_allocation_observe(b->metadata,(uint32_t)sizeof(*b),FX_ALLOC_REQUESTED);
    if(fx_module_bus_init(b,(FxIndexMemory){0,map_allocate,map_release})!=FX_OK){FxAllocationObserver observer=b->metadata;map_release(0,b);fx_allocation_observe(observer,(uint32_t)sizeof(*b),FX_ALLOC_FREE_PENDING);return 0;}
    FxModuleBus *zero=0;
    if(!atomic_compare_exchange_strong_explicit(&s->bus,&zero,b,memory_order_release,memory_order_acquire)){FxAllocationObserver observer=b->metadata;map_release(0,b);fx_allocation_observe(observer,(uint32_t)sizeof(*b),FX_ALLOC_FREE_PENDING);return zero;}
    fx_allocation_observe(b->metadata,(uint32_t)sizeof(*b),FX_ALLOC_PUBLISHED);
    return b;
}
static uint32_t noop(void *p) {
    (void)p;
    return 1u;
}
static FxNativeUi *module_ui(uint32_t, int);
uint64_t fx_z9_menu_navigate(uint32_t page);
static const FxNativeLayoutProfile ui_layout={
    (uintptr_t)FN(0x403d6d38,void *(*)(void)),
    (uintptr_t)FN(0x403b037c,uint32_t (*)(void *,uint32_t)),
    (uintptr_t)FN(0x403b0490,void (*)(void *,uint32_t,uint32_t)),
    (uintptr_t)FN(0x403b05d0,void (*)(void *,uint32_t,uint32_t)),
    (uintptr_t)FN(0x403b03f8,void (*)(void *)),
    (uintptr_t)FN(0x403aed7c,void (*)(void *)),
    (uintptr_t)FN(0x403af040,void (*)(void *)),
    (uintptr_t)FN(0x403b0874,void (*)(void *)),
    (uintptr_t)FN(0x403af294,void (*)(void *))};
static const FxNativeVisibilityProfile ui_visibility={
    (uintptr_t)FN(0x403d6d38,void *(*)(void)),
    (uintptr_t)FN(0x40325fac,void *(*)(uint32_t,void *)),
    (uintptr_t)FN(0x403ec9d0,void (*)(void *,uint32_t)),
    UINT64_C(0x4031f4ac),(uintptr_t)FN(0x4031fa5c,void *(*)(void *,uint32_t)),0x140u,0x4bedu,0x4becu};
static const FxNativeTextProfile ui_text_profile = {
    0x403d6d38u,0x40325facu,0x4031fa5cu,0x403ec9b0u,0x403ec9f0u,
    0x4032be38u,0x4031f4acu,0x4becu,0x140u,0x118u,0x403ecbc4u,0x4031e87cu,0x4031e758u};
static const FxNativeEvent ui_editor_events[]={
    {0x295u,0x405065e0u,0x0u,0u},
    {0x296u,0x405065e4u,0x0u,0u},
    {0x297u,0x405065e8u,0x0u,0u},
    {0x298u,0x405065ecu,0x0u,0u},
    {0x2afu,0x405065f8u,0x0u,0u},
    {0x2b0u,0x405065fcu,0x0u,0u},
    {0x2b1u,0x405065fcu,0x403b217cu,0u},
    {0x2b2u,0x405065f8u,0x403b217cu,0u},
    {0x2c0u,0x40506634u,0x0u,0u},
    {0x2c3u,0x40506638u,0x0u,0u},
    {0x314u,0x40506640u,0x0u,0u},
    {0x316u,0x40506644u,0x0u,0u},
};
static const FxNativeUiEditorProfile ui_editor_profile={
    (const FxNativePage *)(uintptr_t)UINT64_C(0xa0290c7f0),ui_editor_events,
    12u,705u,656u,792u,
    0x4060ae34u,0x4060cf30u,0x4050bcccu,0x403b2814u,0x403d6d38u};
static const FxNativeUiReaderProfile ui_reader_profile={{0x403d6d38u,0x40325facu,0x4032be38u,0x403ec980u,0x40326fb4u,0x40324bf0u,0x4bf1u},0x4031e87cu,0x4031e758u,720u,480u};
static const FxNativeTitleProfile ui_title={0x403d6d38u,0x40325facu,0x4032be38u,0x403ec980u,0x40326fb4u,0x40324bf0u,0x4bf1u};
static const FxNativeDialogStyle ui_dialog={
    (const FxNativeDescriptor *)(uintptr_t)UINT64_C(0xa028be460),
    (const FxNativeRow *)(uintptr_t)UINT64_C(0x42e9b81c),340u,2u};
FxNativeUiDispatch *fx_native_ui_dispatch_domain(void){State *s=state();return s?&s->ui_dispatch:0;}
const uint32_t fx_native_ui_dispatch_metadata[10]={0x46585544u,2u,offsetof(State,ui_dispatch),sizeof(FxNativeUiDispatch),
    offsetof(FxNativeUiDispatch,task),offsetof(FxNativeUiDispatch,pending),offsetof(FxNativeUiDispatch,running),
    offsetof(FxNativeUiDispatch,submitted),offsetof(FxNativeUiDispatch,completed),offsetof(FxNativeUiDispatch,last_task)};
FxResult fx_native_apps_flush_native(void) {
    void *mutex=(void *)(uintptr_t)UINT64_C(0xa032dcb98);
    FN(0x40d30004,void (*)(void *))(mutex);
    FxNativePage *page=FN(0x403d6d38,FxNativePage *(*)(void))();uint32_t destination=0u;
    FxResult result=fx_native_apps_prepare_navigation(fx_native_apps_domain(),page,&destination);
    FN(0x40d2ffb0,void (*)(void *))(mutex);
    if(destination){(void)fx_z9_menu_navigate(destination);return FX_OK;}
    return result;
}
FxResult fx_native_ui_flush_owner(FxPlatformOwner *owner) {
    void *mutex=(void *)(uintptr_t)UINT64_C(0xa032dcb98);
    FN(0x40d30004,void (*)(void *))(mutex);
    FxNativePage *page=FN(0x403d6d38,FxNativePage *(*)(void))();
    FxNativeUi *ui=page?fx_native_ui_find_page(page->identity>>16):0;
    if(ui && ui->owner==owner) {
        uint32_t destination=0u;FxResult redirect=fx_native_ui_retirement_target(ui,page->identity>>16,&destination);
        if(redirect==FX_EMPTY)redirect=fx_native_ui_prepare_navigation(ui,page,&destination);
        if(redirect!=FX_EMPTY) {
            FN(0x40d2ffb0,void (*)(void *))(mutex);
            if(redirect!=FX_OK)return redirect;
            (void)fx_z9_menu_navigate(destination);
            fx_native_ui_refresh_owner(owner);return FX_OK;
        }
    }
    uint32_t changed=0u;
    FxResult refreshed=ui && ui->owner==owner?fx_native_ui_trace(ui,1u,0u,fx_native_ui_recover_values(ui,page->identity>>16,&changed)):FX_EMPTY;
    int repaint=refreshed==FX_OK;
    uint32_t painted=0u;
    if(repaint) {
        const FxNativeDescriptor *d=(const FxNativeDescriptor *)page->descriptor;
        const FxNativeRow *rows=(const FxNativeRow *)d->rows;
        if(changed&FX_NATIVE_UI_TITLE) {
            if(fx_native_ui_trace(ui,2u,0u,fx_native_title_prepare(&ui_title,page))!=FX_OK)repaint=0;
            else painted=1u;
        }
        if(changed&FX_NATIVE_UI_STRUCTURE) {
            for(uint32_t i=0;i<d->count;++i) {
                uint32_t row=rows[i].word[6];int enabled=1;
                int visible=fx_native_ui_row_visible(ui,page->identity>>16,row);
                uint32_t value=fx_native_ui_value(ui,row,&enabled);
                if(fx_native_ui_trace(ui,3u,0u,fx_native_row_visibility(&ui_visibility,page,rows[i].word[6],
                    visible))!=FX_OK ||
                   fx_native_ui_trace(ui,4u,0u,fx_native_row_arrow(&ui_visibility,page,rows[i].word[6],
                    visible && fx_native_ui_row_submenu(ui,rows[i].word[6])))!=FX_OK ||
                   fx_native_ui_trace(ui,5u,0u,fx_native_row_value_visibility(&ui_visibility,page,row,visible && value))!=FX_OK)repaint=0;
            }
            if(repaint && fx_native_ui_trace(ui,6u,0u,fx_native_ui_order_page(ui,page))!=FX_OK)repaint=0;
            if(repaint && fx_native_ui_trace(ui,7u,0u,fx_native_row_reflow(&ui_layout,page))!=FX_OK)repaint=0;
        }
        uint32_t repaint_rows=fx_native_ui_repaint_rows(ui,page,changed);
        for(uint32_t i=0;i<d->count && repaint;++i) {
            uint32_t row_id=rows[i].word[6];int enabled=1;
            if(!(repaint_rows&(1u<<i)))continue;
            uint32_t value_id=fx_native_ui_value(ui,row_id,&enabled);
            if(fx_native_ui_trace(ui,8u,row_id,fx_native_text_prepare(&ui_text_profile,page,row_id,rows[i].word[0],value_id,enabled,ui))!=FX_OK) {
                repaint=0;continue;
            }
            void *row=FN(0x40325fac,void *(*)(uint32_t,void *))(row_id,fx_native_page_rows(page));
            if(row) {
                FN(0x403ec980,void (*)(void *,uint32_t))(row,1u);
                void *value=FN(0x4031fa5c,void *(*)(void *,uint32_t))(row,0x4becu);
                if(value)FN(0x403ec980,void (*)(void *,uint32_t))(value,1u);
                painted=1u;
            }
        }
        if(repaint && painted) {
            FxNativeMenuDrawProfile draw={0x403f196cu,0x403266ecu,0x403ebfb8u,0x403efcecu,0x40c7cb34u,0x40325facu,0x4bf1u,(uintptr_t)fx_native_backdrop_restore};
            if(changed&FX_NATIVE_UI_STRUCTURE)draw.restore=(uintptr_t)fx_native_menu_restore;
            if(fx_native_ui_trace(ui,9u,0u,fx_native_menu_draw(&draw,page))!=FX_OK)repaint=0;
        }
    }
    uint32_t revision=repaint?ui->revision:0u;
    if(repaint && painted)FN(0x403eb0dc,void (*)(void *,uint32_t,uint32_t))
        ((void *)(uintptr_t)UINT64_C(0x403eb078),3u,7u);
    if(repaint)(void)fx_native_ui_trace(ui,10u,0u,fx_native_ui_presented(ui,revision));
    FN(0x40d2ffb0,void (*)(void *))(mutex);
    return repaint?FX_OK:refreshed==FX_OK?FX_BUSY:refreshed;
}
uint32_t fx_native_module_loaded(const char *id) {
    State *s=state();if(!s || atomic_load(&s->phase)!=2u)return 0u;
    uint32_t count=atomic_load_explicit(&s->modules,memory_order_acquire);
    for(uint32_t i=0;i<count;++i)if(s->workers[i] && !memcmp(fx_z9_worker_identity(s->workers[i]),id,16u)) {
        uint32_t phase=fx_z9_worker_phase(s->workers[i]);
        return phase!=FX_Z9_WORKER_EMPTY && phase!=FX_Z9_WORKER_LOADING?1u:0u;
    }
    return 0u;
}
static uint32_t page_opened(void *context) {
    FxNativePage *page = context;
    State *s = state();
    if (!s || !page)
        return 1u;
    if(page==&s->core.page && atomic_load(&s->startup.phase)==FX_STARTUP_IO_ERROR)
        atomic_store(&s->startup.inspect_requested,1u);
    (void)fx_native_ui_page_open(page);
    FxNativeUi *u = fx_native_ui_find_page(page->identity >> 16);
    if(u && !fx_native_ui_page_matches(u,page))return 1u;
    /* Original C532 value object uses child4bec and row children+128;
     * ZR's4fb1/+130 are deliberately not reused. */
    const FxNativeDescriptor *d = (const FxNativeDescriptor *)page->descriptor;
    const FxNativeRow *rows = (const FxNativeRow *)d->rows;
    for (uint32_t i = 0; i < d->count; ++i) {
        uint32_t row = rows[i].word[6], value = 0;
        int enabled = 1;
        if(u) {
            int visible=fx_native_ui_row_visible(u,page->identity>>16,row);
            (void)fx_native_row_visibility(&ui_visibility,page,row,visible);
            (void)fx_native_row_arrow(&ui_visibility,page,row,visible && fx_native_ui_row_submenu(u,row));
            uint32_t value_id=fx_native_ui_value(u,row,&enabled);
            (void)fx_native_row_value_visibility(&ui_visibility,page,row,visible && value_id);
        }
        if (u)
            value = fx_native_ui_value(u, row, &enabled);
        else if(row>=0x7b00u && row<=0x7b14u)value=fx_native_apps_value(s->apps,page,row,&enabled);
        else if (row == 0x7ff1u)
            enabled = !atomic_load(&s->activation) &&
                (atomic_load(&s->startup.phase)==FX_STARTUP_READY || atomic_load(&s->startup.phase)==FX_STARTUP_IO_ERROR);
        else if (row == 0x7ff2u)
            value = TEXT + 10u;
        else if(row==0x7ff5u) {
            value=TEXT+((atomic_load(&s->startup.desired)&FX_STARTUP_AUTO)?14u:15u);
            uint32_t phase=atomic_load(&s->startup.phase);
            enabled=phase==FX_STARTUP_READY || phase==FX_STARTUP_ABNORMAL || phase==FX_STARTUP_RUNNING;
        } else if(row==0x7ff6u) {
            value=TEXT+(atomic_load(&s->startup.phase)==FX_STARTUP_ABNORMAL?19u:18u);
            enabled=atomic_load(&s->startup.phase)==FX_STARTUP_ABNORMAL && atomic_load(&s->startup.flag_known);
        } else if(row==0x7ff7u) {
            value=TEXT+(atomic_load(&s->ptp.enabled)?14u:15u);
            enabled=fx_z9_menu_ptp_active();
        }
        (void)fx_native_text_prepare(&ui_text_profile, page, row, rows[i].word[0], value, enabled,u);
    }
    if(u)(void)fx_native_row_reflow(&ui_layout,page);
    return 1u;
}
static void refresh_core(State *s) {
    /* C532403f1588 uses this exact Nikon GUI mutex with these lock/unlock
     * functions. Draw the current content directly: the stock page-family
     * helper can repaint a different ancestor when extensions own the leaf.
     * A worker must acquire that exclusion before touching cached GUI text;
     * changing phase alone leaves the already rendered Ready string stale. */
    void *mutex=(void *)(uintptr_t)UINT64_C(0xa032dcb98);
    FN(0x40d30004,void (*)(void *))(mutex);
    int repaint=FN(0x403d6d38,void *(*)(void))()==&s->core.page;
    if(repaint) {
        (void)page_opened(&s->core.page);
        void *content=(void *)s->core.page.content;
        const FxNativeDescriptor *d=(const FxNativeDescriptor *)s->core.page.descriptor;
        const FxNativeRow *rows=(const FxNativeRow *)d->rows;
        for(uint32_t i=0;i<d->count;++i) {
            void *row=FN(0x40325fac,void *(*)(uint32_t,void *))(rows[i].word[6],content);
            if(!row)continue;
            FN(0x403ec980,void (*)(void *,uint32_t))(row,1u);
            if(rows[i].word[6]==0x7ff2u || rows[i].word[6]==0x7ff5u || rows[i].word[6]==0x7ff6u) {
                void *value=FN(0x4031fa5c,void *(*)(void *,uint32_t))(row,0x4becu);
                if(value)FN(0x403ec980,void (*)(void *,uint32_t))(value,1u);
            }
        }
        /* Like module UI, use Nikon's dispatcher to bind each original
         * target. A lower painter assumes a prior binding; drawing ancestors
         * into that target can erase cached labels on the current leaf. */
        void *canvases[2]={(void *)s->core.page.surface,(void *)s->core.page.content};
        for(uint32_t i=0;i<2u;++i)if(canvases[i]) {
            FN(0x403f196c,void (*)(void *,uint32_t))(canvases[i],1u);
            (void)FN(0x403266ec,uint32_t (*)(void *,void *,void *))(canvases[i],0,0);
        }
    }
    if(repaint)FN(0x403eb0dc,void (*)(void *,uint32_t,uint32_t))
        ((void *)(uintptr_t)UINT64_C(0x403eb078),3u,7u);
    FN(0x40d2ffb0,void (*)(void *))(mutex);
}
static FxNativeUi *module_ui(uint32_t index,int create) {
    (void)create;State *s=state();if(!s)return 0;
    if(index==UINT32_MAX) {
        if(!s->management)return 0;int ready=atomic_load(&s->phase)==2u;
        for(uint32_t i=0;i<atomic_load(&s->modules);++i)if(s->workers[i] && fx_z9_worker_phase(s->workers[i])==FX_Z9_WORKER_LOADING)ready=0;
        if(ready)fx_module_manager_ready(&s->management->manager);
        return &s->management->ui;
    }
    return index<s->ui_count?s->ui[index]:0;
}
static FxResult ui_space_text(void *context,uint32_t required) {
    return text_ensure(context,required)?FX_OK:FX_BUDGET;
}
static FxResult ui_space_ready(State *s) {
    return fx_native_ui_space_init(&s->ui_space,&s->ui_dispatch,(FxIndexMemory){0,map_allocate,map_release},
        s,ui_space_text,UI_PAGE_BASE,FX_NATIVE_EXTENSION_PAGE_LIMIT,MODULE_TEXT_BASE,MODULE_TEXT_STRIDE,
        MODULE_TEXT_BASE+FX_NATIVE_UI_OWNERS*MODULE_TEXT_STRIDE);
}
static void ui_pager_if_needed(State *s,FxNativeUi *u) {
    uint32_t count=0u;int visible=0;
    if(!u || u->source || fx_ui_presence(u->owner,&count,&visible)!=FX_OK || count<=FX_UI_CONTROLS)return;
    if(ui_space_ready(s)==FX_OK)(void)fx_native_ui_pager_enable(u,&s->ui_space);
}
static FxNativeUi *instance_ui(State *s,uint32_t index,const FxNativePage *parent) {
    FxPlatformOwner *owner=index<atomic_load_explicit(&s->modules,memory_order_acquire) && s->services[index]?&s->services[index]->platform:0;
    if(!owner || (owner->phase!=FX_OWNER_RUNNING && owner->phase!=FX_OWNER_FINISHED))return 0;
    FxNativeUi *cached=(FxNativeUi *)atomic_load_explicit(&owner->native_ui,memory_order_acquire);
    if(cached) {
        uint32_t pinned=0u;for(uint32_t i=0u;i<FX_NATIVE_UI_PAGES;++i)pinned|=cached->pinned[i];
        if(!pinned){cached->profile.page=parent;cached->profile.back=parent->identity>>16;}
        ui_pager_if_needed(s,cached);return cached;
    }
    if(!s->ui) {
        uint32_t count=s->capacity;if(count>FX_NATIVE_UI_OWNERS)count=FX_NATIVE_UI_OWNERS;
        if(!count)return 0;s->ui=allocate(count*(uint32_t)sizeof(*s->ui));if(!s->ui)return 0;
        memset(s->ui,0,count*sizeof(*s->ui));s->ui_capacity=count;
        fx_retained_account(fx_shared_ui_account(&s->accounting),&s->ui_vector_accounted,count*(uint32_t)sizeof(*s->ui));
    }
    if(ui_space_ready(s)!=FX_OK)return 0;
    uint32_t slot=s->ui_count;if(slot>=s->ui_capacity ||
        !text_ensure(s,MODULE_TEXT_BASE+(slot+1u)*MODULE_TEXT_STRIDE))return 0;
    FxAllocationObserver observer=fx_platform_account_observer(owner,"ui.native");
    FxNativeUi *u=allocate((uint32_t)sizeof(*u));if(!u)return 0;memset(u,0,sizeof(*u));
    fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_REQUESTED);
    FxNativeMenuProfile profile={parent,&s->rows[44],(const FxNativeEvent *)(uintptr_t)FX_Z9_MENU_NAVIGATION,
        0x3cau,0x297u,{0x298u,0x29du,0x2b1u,0x2c0u},0x6b8u,parent->identity>>16};
    uintptr_t callbacks[5]={(uintptr_t)page_opened,(uintptr_t)fx_native_ui_page_close,UINT64_C(0x403b2748),(uintptr_t)noop,(uintptr_t)noop};
    uint32_t controls=0u,primary_pages=FX_NATIVE_UI_PAGES;int visible=0;
    if(fx_ui_presence(owner,&controls,&visible)==FX_OK && controls>FX_UI_CONTROLS)primary_pages=2u;
    uint32_t physical=0u;
    if(fx_native_ui_space_take(&s->ui_space,primary_pages,&physical)!=FX_OK){map_release(0,u);fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_FREE_PENDING);return 0;}
    if(fx_native_ui_init(u,owner,&profile,allocate,physical,
        MODULE_TEXT_BASE+slot*MODULE_TEXT_STRIDE,0x8000u+slot*64u,fx_native_ui_actions,fx_native_ui_options,callbacks)!=FX_OK){fx_native_ui_space_rollback(&s->ui_space,physical,primary_pages);map_release(0,u);fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_FREE_PENDING);return 0;}
    if(primary_pages==2u)u->picker_page=physical+1u;
    u->dialog=&ui_dialog;u->editor_profile=&ui_editor_profile;u->reader_profile=&ui_reader_profile;
    u->request_refresh=fx_native_ui_refresh_owner;
    if(fx_ui_dispatch_bind_page(&s->ui_dispatch,u)!=FX_OK){fx_native_ui_space_rollback(&s->ui_space,physical,primary_pages);map_release(0,u);fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_FREE_PENDING);return 0;}
    fx_native_ui_identity(u,fx_z9_worker_identity(s->workers[index]));
    s->ui[slot]=u;s->ui_count=slot+1u;
    atomic_store_explicit(&owner->native_ui,(uintptr_t)u,memory_order_release);
    fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_PUBLISHED);ui_pager_if_needed(s,u);return u;
}

FxNativeApps *fx_native_apps_domain(void){State *s=state();return s?s->apps:0;}
static uint32_t apps_count(void *context){State *s=context;return atomic_load(&s->phase)==2u?atomic_load_explicit(&s->modules,memory_order_acquire):0u;}
static FxResult apps_entry(void *context,uint32_t index,const FxNativePage *parent,int selected,FxNativeAppEntry *entry) {
    State *s=context;FxPlatformOwner *owner=index<atomic_load_explicit(&s->modules,memory_order_acquire) && s->services[index]?&s->services[index]->platform:0;if(!owner)return FX_EMPTY;
    if(owner->phase!=FX_OWNER_RUNNING && owner->phase!=FX_OWNER_FINISHED)return FX_EMPTY;
    uint32_t controls=0u;int visible=0;FxResult present=fx_ui_presence(owner,&controls,&visible);if(present!=FX_OK)return present;
    if(controls && !visible)return FX_EMPTY;
    int legacy=0;if(!controls)return FX_EMPTY;
    if(!parent){memset(entry,0,sizeof(*entry));return FX_OK;}
    FxNativeUi *u=instance_ui(s,index,parent);if(!u)return FX_BUDGET;
    if(selected && controls && !fx_native_ui_page(u,u->page_base))return FX_BUDGET;
    if(selected && controls && !fx_native_ui_root_visible(u))return FX_EMPTY;
    *entry=(FxNativeAppEntry){u->text_base+FX_NATIVE_UI_TEXTS,legacy?u->text_base+FX_NATIVE_UI_TEXTS+1u:0u,
        legacy?(uint32_t)u->profile.stay:u->page_base,
        !atomic_load(&s->shutdown) && (owner->phase==FX_OWNER_RUNNING || (!legacy && owner->phase==FX_OWNER_FINISHED)),&u->app_links,u->observer};
    return FX_OK;
}
static FxResult apps_action(void *context,uint32_t index){State *s=context;return index<atomic_load(&s->modules) && s->workers[index]?fx_z9_worker_action(s->workers[index],1u):FX_EMPTY;}
static FxNativePage *apps_page(State *s,uint32_t page) {
    if(!s->apps) {
        FxNativeApps *apps=&s->apps_storage;
    FxNativeMenuProfile profile={stock(2u,SETUP),&s->rows[44],(const FxNativeEvent *)(uintptr_t)FX_Z9_MENU_NAVIGATION,
        0x3cau,0x297u,{0x298u,0x29du,0x2b1u,0x2c0u},0x6b8u,SETUP};
    uintptr_t callbacks[5]={(uintptr_t)page_opened,(uintptr_t)fx_native_ui_page_close,UINT64_C(0x403b2748),(uintptr_t)noop,(uintptr_t)noop};
        if(fx_native_apps_init(apps,&profile,callbacks,s,allocate,apps_count,apps_entry,apps_action,
            TEXT+1u,TEXT+21u,TEXT+22u,TEXT+5u,TEXT+4u,TEXT+24u,TEXT+25u)!=FX_OK)return 0;
        apps->capture_task=fx_native_ui_capture_task;
        apps->embedded=1u;apps->account=fx_shared_ui_account(&s->accounting);s->apps=apps;fx_native_apps_account_retained(apps);
    }
    FxNativePage *result=fx_native_apps_page(s->apps,page);
    if(s->apps->error && s->apps->error!=FX_BUDGET && s->apps->error!=FX_BUSY) {
        fx_native_module_fault(0,(FxResult)s->apps->error);
    }
    return result;
}
static int module_text(State *s,uint32_t id,const uint16_t **text,uint32_t *bytes) {
    (void)s;
    if(fx_native_ui_pager_text(&s->ui_space,id,text,bytes))return 1;
    if(id<MODULE_TEXT_BASE)return 0;
    uint32_t slot=(id-MODULE_TEXT_BASE)/MODULE_TEXT_STRIDE,offset=(id-MODULE_TEXT_BASE)%MODULE_TEXT_STRIDE;
    FxNativeUi *u=module_ui(slot,0);static const uint16_t empty[]=u"";*text=empty;*bytes=0u;
    if(!u)return 1;
    if(offset<FX_NATIVE_UI_TEXTS){(void)fx_native_ui_text(u,id,text,bytes);return 1;}
    if(offset==FX_NATIVE_UI_TEXTS) {
        *text=u->contribution_title;uint32_t n=0u;while(n<FX_UI_TEXT && (*text)[n])++n;*bytes=n*2u;
    }
    return 1;
}

typedef struct {
    int32_t fd;
    FxFileObserver observer;
} Directory;
static FxResult dir_open(void *context) {
    Directory *d = context;
    d->fd = -1;
    uint32_t r = FN(0x402dd0d4, uint32_t (*)(const char *, int32_t *))("A:\\FENESTRA", &d->fd);
    return r == UINT32_C(0x80000010) ? FX_EMPTY : r || d->fd < 0 ? FX_IO : FX_OK;
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static FxResult dir_next(void *context, FxModuleDirectoryEntry *out) {
    Directory *d = context;
    uint8_t data[0x220] __attribute__((aligned(8))) = {0};
    uint32_t r = FN(0x402dd184, uint32_t (*)(int32_t, void *))(d->fd, data);
    if (r == UINT32_C(0x80000030))
        return FX_EMPTY;
    if (r)
        return FX_IO;
    memset(out, 0, sizeof(*out));
    out->attributes = data[0x20e];
    out->bytes = (uint64_t)u32(data + 0x218) | (uint64_t)u32(data + 0x21c) << 32;
    uint32_t i = 0;
    for (; i < 256u; ++i) {
        uint32_t ch = (uint32_t)data[14u + i * 2u] | (uint32_t)data[15u + i * 2u] << 8;
        if (!ch)
            break;
        if (i >= 31u || ch > 127u) {
            out->name[0] = 0;
            return FX_OK;
        }
        out->name[i] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32u : ch);
    }
    if (!i)
        for (i = 0; i < 13u && data[i]; ++i)
            out->name[i] = (char)(data[i] >= 'A' && data[i] <= 'Z' ? data[i] + 32u : data[i]);
    return FX_OK;
}
static FxResult dir_close(void *context) {
    Directory *d = context;
    uint32_t r = FN(0x402dd1d4, uint32_t (*)(int32_t))(d->fd);
    d->fd = -1;
    return r ? FX_CLOSE : FX_OK;
}
static FxResult module_read(void *context, const char *file, uint8_t *out, uint32_t capacity,
                            uint32_t *bytes) {
    Directory *directory=context;
    *bytes = 0;
    if (!fx_service_name(file, 32u, 1) || !capacity || capacity > FX_NATIVE_MODULE_PACKAGE_LIMIT+1u)
        return FX_ARGUMENT;
    char path[64] = "A:\\FENESTRA\\";
    uint32_t n = 0;
    while (n < 32u && file[n])
        ++n;
    memcpy(path + 12u, file, n + 1u);
    int32_t fd = FN(0x402dc960, int32_t (*)(const char *, uint32_t, uint32_t))(path, 0, 0);
    FxCapacityClaim claim=fx_file_observe_open(directory->observer,fd);
    if (fd < 0)
        return FX_IO;
    uint32_t error = 0, calls = 0;
    while (*bytes < capacity) {
        if (++calls > FX_READ_CALL_LIMIT) {
            error = 1;
            break;
        }
        uint32_t done = 0, room = capacity - *bytes;
        error = FN(0x402dcb30, uint32_t (*)(int32_t, void *, uint32_t, uint32_t *))(fd, out + *bytes, room,
                                                                                    &done);
        if (error || done > room) {
            error = 1;
            break;
        }
        *bytes += done;
        if (!done)
            break;
    }
    uint32_t closed = FN(0x402dc9c0, uint32_t (*)(int32_t))(fd);
    fx_file_observe_close(directory->observer,&claim,closed);
    return closed ? FX_CLOSE : error ? FX_IO : FX_OK;
}
static void *index_allocate(void *context,uint32_t bytes){(void)context;return allocate(bytes);}
static void index_release(void *context,void *memory){(void)context;FN(0x40d30498,void (*)(void *))(memory);}
typedef struct {State *state;FxZ9ModuleWorker *worker;FxPlatformOwner *owner;} LoadWait;
static int load_cancelled(void *context){LoadWait *wait=context;return atomic_load(&wait->state->shutdown)!=0u;}
static FxResult load_yield(void *context){(void)context;return fx_z9_task_delay_ticks(1u);}
static FxResult load_poll(void *context) {
    LoadWait *wait=context;uint32_t phase=fx_z9_worker_phase(wait->worker);
    if(phase==FX_Z9_WORKER_LOADING)return FX_BUSY;
    if(phase==FX_Z9_WORKER_PANIC) {
        FxResult error=(FxResult)fx_z9_worker_error(wait->worker);
        if(error==FX_OK)error=FX_INTEGRITY;
        fx_native_module_fault(wait->owner,error);return error;
    }
    return phase==FX_Z9_WORKER_ACTIVE || phase==FX_Z9_WORKER_FINISHED?FX_OK:FX_STATE;
}
static void load_once(State *s) {
    if(!atomic_load_explicit(&s->accounting.ready,memory_order_acquire)) {
        (void)fx_shared_accounts_init(&s->accounting,(FxIndexMemory){0,map_allocate,map_release},(uint32_t)sizeof(*s),(uint32_t)sizeof(s->ui_dispatch.tasks));
        fx_linux_account(&s->linux,fx_account_observer(fx_shared_account_row(&s->accounting.state,"debug.linux")));
        fx_linux_account_packets(&s->linux,(FxIndexMemory){0,map_allocate,map_release},
            fx_account_observer(fx_shared_account_row(&s->accounting.state,"debug.linux.packets")),
            fx_account_observer(fx_shared_account_row(&s->accounting.state,"debug.linux.metadata")));
        if(ui_space_ready(s)==FX_OK)(void)fx_platform_capacity_init(&s->capacity_registry,&s->capacity_domains,(FxIndexMemory){0,map_allocate,map_release},
            fx_account_observer(fx_shared_account_row(&s->accounting.state,"capacity.metadata")),&s->ui_space);
        s->hooks.page_observer=fx_account_observer(&s->accounting.pages);s->hooks.owner_account=fx_account_hook_storage;
        s->ui_dispatch.tasks.owner_account=fx_account_task_storage;
        /* Snapshot still-live immutable generations only, after Activate.
         * Publication callbacks share these tags with the GUI writer. */
        for(const FxNativeTextBank *b=atomic_load_explicit(&s->text_bank,memory_order_seq_cst);b;b=b->previous)
            fx_retained_account(fx_shared_text_account(&s->accounting),(_Atomic uint32_t *)&b->accounted,b->bytes);
        fx_native_apps_account_retained(atomic_load_explicit(&s->apps,memory_order_seq_cst));
        FxNativeModules *m=atomic_load_explicit(&s->management,memory_order_seq_cst);
        if(m){fx_retained_account(fx_shared_ui_account(&s->accounting),&s->management_accounted,(uint32_t)sizeof(*m));fx_native_ui_account_retained(&m->ui);
            (void)fx_module_manager_account(&m->manager,fx_account_observer(&s->accounting.indexes),fx_account_observer(&s->accounting.inspect));}
        if(atomic_load_explicit(&s->task_stack,memory_order_seq_cst))
            fx_retained_account(fx_shared_loader_account(&s->accounting),&s->stack_accounted,65536u+16u);
        if(s->panic)fx_retained_account(fx_shared_loader_account(&s->accounting),&s->panic_accounted,(uint32_t)sizeof(*s->panic));
        atomic_fetch_or(&s->accounting.state.flags,FX_ACCOUNT_SEEDED);
    }
    FxModuleInventory inventory={0};
    Directory directory = {.fd=-1,.observer=fx_capacity_file_observer(&s->capacity_registry)};
    FxModuleDirectory io = {&directory, dir_open, dir_next, dir_close, module_read};
    const FxPlatform *platform = fx_platform_target();
    FxResult r = fx_modules_discover_accounted(&io, platform->identity, FX_SDK_CAPS, s->scratch, sizeof(s->scratch),
        &inventory,(FxIndexMemory){0,index_allocate,index_release},fx_account_observer(&s->accounting.indexes),fx_account_observer(&s->accounting.inspect));
    if(r==FX_OK && inventory.count>FX_MODULE_OWNER_LIMIT)r=FX_BUDGET;
    if(r==FX_OK && inventory.count) {
        uint32_t bytes=inventory.count*2u*(uint32_t)sizeof(uintptr_t);
        s->workers=allocate(bytes);
        if(!s->workers)r=FX_BUDGET;
        else {FxAllocationObserver observer=fx_account_observer(&s->accounting.loader);
            fx_allocation_observe(observer,bytes,FX_ALLOC_REQUESTED);
            memset(s->workers,0,bytes);s->services=(FxZ9Services **)(s->workers+inventory.count);s->capacity=inventory.count;
            fx_allocation_observe(observer,bytes,FX_ALLOC_PUBLISHED);}
    }
    if (r != FX_OK && r != FX_EMPTY) {
        atomic_store(&s->error, (uint32_t)r);
        /* Failed close leaves native ownership uncertain: never reopen it. */
        atomic_store(&s->phase, r == FX_CLOSE ? 5u : 3u);
    } else {
        for (uint32_t i = 0; i < inventory.count && !atomic_load(&s->shutdown); ++i) {
            FxDiscoveredModule *entry = &inventory.entries[i];
            if (!entry->enabled) {
                ++s->skipped;
                continue;
            }
            FxExecLayout layout;
            r=fx_z9_session_layout_stacks(entry->bytes,entry->image_bytes,entry->stacks,&layout);
            if(r!=FX_OK)break;
            uint32_t bytes = fx_z9_worker_bytes(), session_bytes = layout.bytes + 4096u;
            void *w = allocate(bytes), *session = allocate(session_bytes);
            FxZ9Services *services = allocate((uint32_t)sizeof(*services));
            if (!w || !session || !services) {
                /* Nothing has been initialized, registered or handed to a
                 * task. These allocations have no escaping references. */
                if(w)index_release(0,w);
                if(session)index_release(0,session);
                if(services)index_release(0,services);
                r = FX_BUDGET;
                break;
            }
            memset(w, 0, bytes);
            memset(session, 0, session_bytes);
            memset(services, 0, sizeof(*services));
            uint32_t caps = (entry->required | entry->optional) & FX_SDK_CAPS;
            s->services[i]=services; /* Retain failed-start ownership as well. */
            r=fx_runtime_accounts_init(&services->accounting,i+1u,1u,(FxIndexMemory){0,map_allocate,map_release});
            if(r==FX_OK)services->accounting.state.shared=&s->accounting.state;
            if(r==FX_OK)(void)fx_account_acquire(&services->accounting.allocation,(uint64_t)bytes+session_bytes+sizeof(*services),3u,1u);
            if(r==FX_OK)r = fx_z9_services_init(services, i + 1u, 1u, caps);
            if(r==FX_OK)services->platform.accounting=&services->accounting.state;
            if(r==FX_OK)services->platform.capacity=&s->capacity_registry;
            if(r==FX_OK)fx_account_owner_bind(&services->platform);
            if(r==FX_OK)r=fx_ui_dispatch_register(&s->ui_dispatch,&services->platform);
            if (r != FX_OK)
                break;
            r = fx_z9_worker_start(w, bytes, session, session_bytes, entry, i + 1u, 1u, caps,
                                   (FxServiceBackend){services, fx_z9_service_call, fx_z9_service_stop},
                                   &s->workers[i]);
            if(s->workers[i])atomic_store_explicit(&s->modules,i+1u,memory_order_release);
            if(r!=FX_OK)break;
            /* Package scratch has been released before ACTIVE. Keep startup
             * scratch bounded even when two large native reads would overlap. */
            LoadWait wait={s,s->workers[i],&services->platform};
            r=fx_load_sequence_wait((FxLoadSequence){&wait,load_poll,load_yield,load_cancelled});
            if(r==FX_INTERRUPTED && atomic_load(&s->shutdown)) {
                r=atomic_load(&s->phase)==5u?(FxResult)atomic_load(&s->error):FX_OK;break;
            }
            if(r!=FX_OK)break;
        }
        if (r != FX_OK && r != FX_EMPTY) {
            /* Latch the controller's synchronous failure now, before a
             * retained worker reports a consequential allocation failure. */
            fx_native_module_fault(0,r);
        } else {
            /* A UI/native callback can publish Panic while modules start.
             * Loading->Active must never overwrite that concurrent fault. */
            uint32_t loading=1u;
            (void)atomic_compare_exchange_strong(&s->phase,&loading,2u);
        }
    }
    /* Every started worker owns a copied descriptor and identity. */
    fx_modules_destroy(&inventory);refresh_core(s);
}
static FxResult start_modules(void *context) {
    State *s=context;
    if(atomic_load(&s->shutdown))return FX_STATE;
    if(s->management) {
        FxResult r=fx_module_manager_freeze(&s->management->manager);
        if(r!=FX_OK)return r;
    }
    uint32_t zero=0;
    if(!atomic_compare_exchange_strong(&s->activation,&zero,1u))return FX_STATE;
    /* Reserve the error surface before starting any module. No boot-time
     * scene or extra pre-Activate allocation; late callbacks retain this RAM. */
    s->panic=allocate((uint32_t)sizeof(*s->panic));
    if(!s->panic)return FX_BUDGET;
    fx_native_panic_page_init(s->panic);
    fx_retained_account(fx_shared_loader_account(&s->accounting),&s->panic_accounted,(uint32_t)sizeof(*s->panic));
    atomic_store(&s->phase,1u);
    load_once(s);
    return atomic_load(&s->phase)==2u?FX_OK:FX_IO;
}
static FxResult startup_health(void *context) {
    State *s=context;
    if(atomic_load(&s->phase)==5u)return FX_INTEGRITY;
    int pending=0;
    for(uint32_t i=0;i<atomic_load(&s->modules);++i) {
        uint32_t phase=fx_z9_worker_phase(s->workers[i]);
        if(phase==FX_Z9_WORKER_PANIC)return (FxResult)fx_z9_worker_error(s->workers[i]);
        if(phase!=FX_Z9_WORKER_ACTIVE && phase!=FX_Z9_WORKER_FINISHED)pending=1;
    }
    return pending?FX_BUSY:FX_OK;
}
static FxResult startup_stop(void *context) {
    State *s=context;
    atomic_store(&s->shutdown,1u);
    if(s->management)fx_module_manager_shutdown(&s->management->manager);
    fx_linux_revoke();
    int pending=!fx_rtos_quiesced() || !fx_linux_quiesced();
    for(uint32_t i=0;i<atomic_load(&s->modules);++i) {
        fx_z9_worker_shutdown(s->workers[i]);
        uint32_t phase=fx_z9_worker_phase(s->workers[i]);
        if(phase==FX_Z9_WORKER_PANIC)return (FxResult)fx_z9_worker_error(s->workers[i]);
        if(phase!=FX_Z9_WORKER_QUIESCED)pending=1;
    }
    return pending?FX_BUSY:FX_OK;
}
static uint64_t startup_token(void *context) {
    (void)context;uint64_t counter;__asm__ volatile("mrs %0,cntpct_el0":"=r"(counter));return counter;
}
static void show_panic(State *s) {
    PanicPage *p=s->panic;
    if(!p || atomic_load_explicit(&s->phase,memory_order_acquire)!=5u)return;
    if(!p->captured) {
        uint64_t record=atomic_load_explicit(&s->fault_record,memory_order_acquire);
        if(!record)return;
        uint32_t owner=(uint32_t)(record>>32),count=atomic_load_explicit(&s->modules,memory_order_acquire);
        p->report=(FxPanicReport){.module="Fenestra",.error=(FxResult)(uint32_t)record,.module_result=INT32_MIN};
        for(uint32_t i=0;i<count;++i)if(i+1u==owner) {
            if(fx_z9_worker_panic_report(s->workers[i],&p->report)!=FX_OK)return;
            break;
        }
        p->captured=1u;
    }
    (void)fx_native_panic_poll(p);
}
static void loader(void *context) {
    State *s = context;
    for (;;) {
        uint32_t old_phase=atomic_load(&s->startup.phase),old_desired=atomic_load(&s->startup.desired),
            old_error=atomic_load(&s->startup.error);
        fx_startup_service_tick(&s->startup);
        fx_z9_ptp_poll();
        uint32_t count = atomic_load_explicit(&s->modules, memory_order_acquire);
        for (uint32_t i = 0; i < count; ++i)
            if (s->workers[i]) {
                if (atomic_load(&s->shutdown))
                    fx_z9_worker_shutdown(s->workers[i]);
                if (fx_z9_worker_phase(s->workers[i]) == FX_Z9_WORKER_PANIC) {
                    fx_native_module_fault(&s->services[i]->platform,(FxResult)fx_z9_worker_error(s->workers[i]));
                }
            }
        /* Prefer the originating worker snapshot over startup's summarized
         * health result when both become visible in the same loader tick. */
        if(atomic_load(&s->startup.phase)==FX_STARTUP_FAULT) {
            uint32_t error=atomic_load(&s->error);
            fx_native_module_fault(0,(FxResult)(error?error:atomic_load(&s->startup.error)));
        }
        show_panic(s);
        if(!atomic_load(&s->startup.shutdown) &&
           (old_phase!=atomic_load(&s->startup.phase) || old_desired!=atomic_load(&s->startup.desired) ||
            old_error!=atomic_load(&s->startup.error)))refresh_core(s);
        (void)fx_z9_task_delay_ticks(10u);
    }
}
static uint32_t activate(void *p) {
    (void)p;
    State *s = state();
    if (!s)
        return 0;
    (void)fx_startup_service_request(&s->startup,FX_START_ACTIVATE);
    return 1u;
}
static uint32_t auto_on(void *p) {(void)p;State *s=state();if(s)(void)fx_startup_service_request(&s->startup,FX_START_AUTO_ON);return 1u;}
static uint32_t auto_off(void *p) {(void)p;State *s=state();if(s)(void)fx_startup_service_request(&s->startup,FX_START_AUTO_OFF);return 1u;}
static uint32_t usb_toggle(void *p) {
    (void)p;State *s=state();if(!s || !fx_z9_menu_ptp_active())return 1u;
    fx_ptp_enable(&s->ptp,!atomic_load(&s->ptp.enabled));refresh_core(s);return 1u;
}
static uint32_t clear_flag(void *p) {(void)p;State *s=state();if(s)(void)fx_startup_service_request(&s->startup,FX_START_CLEAR);return 1u;}
static uint32_t manage_modules(void *p) {
    (void)p;State *s=state();if(!s || atomic_load(&s->shutdown))return 0u;
    if(!s->management) {
        FxNativeModules *m=allocate((uint32_t)sizeof(*m));if(!m)return 0u;
        FxNativeMenuProfile profile={&s->core.page,&s->rows[44],
            (const FxNativeEvent *)(uintptr_t)FX_Z9_MENU_NAVIGATION,
            0x3cau,0x297u,{0x298u,0x29du,0x2b1u,0x2c0u},0x6b8u,FENESTRA};
        uintptr_t callbacks[5]={(uintptr_t)page_opened,(uintptr_t)fx_native_ui_page_close,
            (uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop};
        if(fx_native_modules_init(m,fx_platform_target(),&profile,allocate,
            UI_TEXT_BASE,0x7d00u,callbacks,
            atomic_load(&s->activation)!=0u)!=FX_OK){map_release(0,m);return 0u;}
        m->ui.retained_account=fx_shared_ui_account(&s->accounting);
        m->ui.editor_profile=&ui_editor_profile;
        m->file_observer=fx_capacity_file_observer(&s->capacity_registry);
        if(atomic_load_explicit(&s->accounting.ready,memory_order_acquire))
            (void)fx_module_manager_account(&m->manager,fx_account_observer(&s->accounting.indexes),fx_account_observer(&s->accounting.inspect));
        s->management=m;
        fx_retained_account(fx_shared_ui_account(&s->accounting),&s->management_accounted,(uint32_t)sizeof(*m));
    }
    if(fx_ui_dispatch_register(&s->ui_dispatch,&s->management->manager.ui)!=FX_OK ||
       fx_ui_dispatch_bind_page(&s->ui_dispatch,&s->management->ui)!=FX_OK)return 0u;
    (void)fx_native_modules_open(s->management);return 1u;
}
static void install(void) {
    if (state())
        return;
    FxNativePage *p = stock(2u, SETUP);
    if (!p || p->descriptor != UINT64_C(0xa028d1a10))
        return;
    const FxNativeDescriptor *d = (const FxNativeDescriptor *)p->descriptor;
    if (d->count != 45u || d->header != UINT64_C(0x4317e310) || d->rows != UINT64_C(0x4317e420) ||
        d->events != UINT64_C(0x4317ebe0))
        return;
    const FxNativeRow *rows = (const FxNativeRow *)d->rows;
    if (rows[0].word[0] != 651u || rows[42].word[0] != 906u)
        return;
    const uint8_t *font = *(const uint8_t *volatile *)(uintptr_t)UINT64_C(0xa02f4ab30);
    const uintptr_t *attributes = *(const uintptr_t *volatile *)(uintptr_t)UINT64_C(0xa02f4ab40);
    if (!font || !attributes)
        return;
    for (uint32_t i = 0; i < LANGUAGES; ++i)
        if (!attributes[i])
            return;
    State *s = allocate((uint32_t)sizeof(*s));
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->setup = *d;
    const FxNativeEvent *nav = (const FxNativeEvent *)(uintptr_t)FX_Z9_MENU_NAVIGATION;
    FxNativeMenuProfile profile = {p,      &rows[42], nav, 0x3cau, 0x297u, {0x298u, 0x29du, 0x2b1u, 0x2c0u},
                                   0x6b8u, SETUP};
    uintptr_t callbacks[5] = {(uintptr_t)page_opened, (uintptr_t)fx_native_ui_page_close, UINT64_C(0x403b2748),
                              (uintptr_t)noop, (uintptr_t)noop};
    FxNativeEntry core[] = {{TEXT + 2u, 0x7ff1u, 0, 0, (uintptr_t)activate, profile.stay},
                            {TEXT + 3u, 0x7ff2u, 0, 1, (uintptr_t)noop, profile.stay},
                            {TEXT + 11u, 0x7ff4u, 1, 0, (uintptr_t)manage_modules, FX_NATIVE_MANAGER_PAGE_BASE},
                            {TEXT + 12u, 0x7ff5u, 1, 1, (uintptr_t)noop, FX_NATIVE_STARTUP_AUTO_PAGE},
                            {TEXT + 13u, 0x7ff6u, 1, 1, (uintptr_t)noop, FX_NATIVE_STARTUP_GUARD_PAGE},
                            {TEXT + 20u, 0x7ff7u, 0, 1, (uintptr_t)usb_toggle, profile.stay},
                            {TEXT + 4u, 0x7ff3u, 0, 0, (uintptr_t)noop, profile.back}};
    FxNativeEntry wonder[] = {{TEXT + 5u, 0x7fc0u, 0, 0, (uintptr_t)noop, profile.stay},
                              {TEXT + 4u, 0x7ff3u, 0, 0, (uintptr_t)noop, profile.back}};
    if (fx_native_menu_build(&s->core, &profile, FENESTRA, TEXT, core, 7u, callbacks) != FX_OK ||
        fx_native_menu_build(&s->wonder, &profile, WONDER, TEXT + 1u, wonder, 2u, callbacks) != FX_OK)
        return;
    FxNativeMenuProfile child=profile;child.back=FENESTRA;
    FxNativeEntry automatic[]={{TEXT+14u,0x7fd0u,0,0,(uintptr_t)auto_on,FENESTRA},
                              {TEXT+15u,0x7fd1u,0,0,(uintptr_t)auto_off,FENESTRA}};
    FxNativeEntry guard[]={{TEXT+17u,0x7fd2u,0,0,(uintptr_t)noop,FENESTRA},
                          {TEXT+16u,0x7fd3u,0,0,(uintptr_t)clear_flag,FENESTRA}};
    if(fx_native_menu_build(&s->auto_menu,&child,FX_NATIVE_STARTUP_AUTO_PAGE,TEXT+12u,automatic,2u,callbacks)!=FX_OK ||
       fx_native_menu_build(&s->guard_menu,&child,FX_NATIVE_STARTUP_GUARD_PAGE,TEXT+13u,guard,2u,callbacks)!=FX_OK)return;
    fx_startup_service_init(&s->startup,UINT32_C(0x00090532),
        (FxStartupBackend){s,start_modules,startup_health,startup_stop,startup_token});
    extern const FxStartupFileProfile fx_z9_startup_file_profile;
    s->startup_files=(FxStartupFiles){.profile=&fx_z9_startup_file_profile,.shutdown=&s->startup.shutdown,
        .cleanup_window=&s->startup.cleanup_window,.observer=fx_capacity_file_observer(&s->capacity_registry)};
    if(fx_startup_files(&s->startup.io,&s->startup_files)!=FX_OK)return;
    fx_z9_ptp_init(&s->ptp);
    memcpy(s->rows + 2u, rows, 45u * sizeof(*rows));
    memcpy(s->events + 2u, (const void *)d->events, 45u * sizeof(uintptr_t));
    for (uint32_t r = 0; r < 2u; ++r) {
        s->rows[r] = rows[42];
        s->rows[r].word[0] = TEXT + r;
        s->rows[r].word[2] = 65535u;
        s->rows[r].word[6] = 0x7ff0u + r;
        if(fx_native_menu_link(s->entry_events[r],nav,profile.terminal,profile.select_events,
                               (uintptr_t)noop,r?WONDER:FENESTRA)!=FX_OK)return;
        s->events[r] = (uintptr_t)s->entry_events[r];
    }
    s->setup.rows = (uintptr_t)s->rows;
    s->setup.events = (uintptr_t)s->events;
    s->setup.count = 47u;
    FxNativeTextBank *bank=0;
    if(fx_native_text_bank_create((FxIndexMemory){0,map_allocate,map_release},font,attributes,TEXT,
        LANGUAGES,TEXT+TEXT_COUNT,font[906],((const uint8_t *)attributes[1])[651],&bank)!=FX_OK)return;
    atomic_store_explicit(&s->text_bank,bank,memory_order_seq_cst);
    if(fx_text_regions_add(&s->text_regions,&s->state_text_region,s,sizeof(*s))!=FX_OK)return ;
    if(fx_text_regions_add(&s->text_regions,&s->image_text_region,__fx_core_code_begin,
        (uintptr_t)__fx_core_code_end-(uintptr_t)__fx_core_code_begin)!=FX_OK)return ;
    s->ui_dispatch.text_regions=&s->text_regions;
    s->magic = MAGIC;
    s->installed = 1u;
    __asm__ volatile("dmb ish" ::: "memory");
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa02f4ab30) = (uintptr_t)bank->fonts;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa02f4ab40) = (uintptr_t)bank->attributes;
    atomic_store_explicit((_Atomic uintptr_t *)&p->descriptor, (uintptr_t)s, memory_order_release);
    /* AppFirstStartComplete has returned. Only the journal observer starts;
     * it reads card settings but never arms/loads while Auto is Off. */
    void *stack=allocate(65536u+16u);s->task_stack=stack;
    if(stack)fx_retained_account(fx_shared_loader_account(&s->accounting),&s->stack_accounted,65536u+16u);
    FxResult started=stack?fx_z9_task_start(&s->task,loader,s,
        (void *)(((uintptr_t)stack+15u)&~(uintptr_t)15u),65536u):FX_BUDGET;
    if(started!=FX_OK) {
        atomic_store(&s->startup.error,(uint32_t)started);
        atomic_store(&s->startup.phase,FX_STARTUP_FAULT);
        fx_native_module_fault(0,started);
    }
}
FxNativePage *fx_z9_menu_page(uint32_t group, uint32_t id) {
    id &= 65535u;
    State *s = state();
    if (group == 2u && id >= FENESTRA && id < FX_NATIVE_EXTENSION_PAGE_LIMIT && !s)
        return 0;
    if (group == 2u && s) {
        if(id==FX_NATIVE_STARTUP_AUTO_PAGE) {
            s->auto_menu.page.selected=(uint16_t)((atomic_load(&s->startup.desired)&FX_STARTUP_AUTO)?0u:1u);
            return &s->auto_menu.page;
        }
        if(id==FX_NATIVE_STARTUP_GUARD_PAGE) {s->guard_menu.page.selected=0;return &s->guard_menu.page;}
        if(id>=FX_NATIVE_MANAGER_PAGE_BASE && id<FX_NATIVE_MANAGER_PAGE_BASE+FX_NATIVE_UI_PAGES) {
            FxNativeUi *u=module_ui(UINT32_MAX,0);return u?fx_native_ui_page(u,id):0;
        }
        if (id >= UI_PAGE_BASE && id < FX_NATIVE_EXTENSION_PAGE_LIMIT) {
            FxNativeUi *u = fx_ui_dispatch_find_page(&s->ui_dispatch,id);
            return u ? fx_native_ui_page(u, id) : 0;
        }
        if (id == FENESTRA)
            return &s->core.page;
        if(fx_native_apps_is_page(id))return apps_page(s,id);
        if(id>=FENESTRA && id<FX_NATIVE_EXTENSION_PAGE_LIMIT)return 0;

    }
    return stock(group, id);
}
static void fx_z9_menu_text_impl(int32_t language, int32_t id, const uint16_t **text, uint32_t *bytes) {
    State *s = state();
    if(s && id>=(int32_t)MODULE_TEXT_BASE && module_text(s,(uint32_t)id,text,bytes))return;
    if(s && id>=(int32_t)UI_TEXT_BASE && id<(int32_t)(UI_TEXT_BASE+FX_NATIVE_UI_TEXTS)) {
        FxNativeUi *u=module_ui(UINT32_MAX,0);if(fx_native_ui_text(u,(uint32_t)id,text,bytes))return;
        static const uint16_t empty[]=u"";*text=empty;*bytes=0u;return;
    }
    if (s && id >= (int32_t)TEXT && id < (int32_t)(TEXT + 26u)) {
        const uint16_t *v = u"";
        uint32_t n = 0;
#define LABEL(t)                                                                                             \
    do {                                                                                                     \
        static const uint16_t x[] = u##t;                                                                    \
        v = x;                                                                                               \
        n = (uint32_t)sizeof(x) - 2u;                                                                        \
    } while (0)
        switch ((uint32_t)id - TEXT) {
        case 0:
            LABEL("Fenestra");
            break;
        case 1:
            LABEL("WonderLantern");
            break;
        case 2:
            if(atomic_load(&s->startup.phase)==FX_STARTUP_ABNORMAL) {LABEL("Disabled due to abnormal shutdown");}
            else {LABEL("Activate");}
            break;
        case 3:
            LABEL("Status");
            break;
        case 4:
            LABEL("Back");
            break;
        case 5:
            LABEL("No loaded modules");
            break;
        case 11:
            LABEL("Modules");
            break;
        case 12:LABEL("Auto activate");break;
        case 13:LABEL("Shutdown guard");break;
        case 14:LABEL("On");break;
        case 15:LABEL("Off");break;
        case 16:LABEL("Clear abnormal flag");break;
        case 17:LABEL("Cancel");break;
        case 18:LABEL("Normal");break;
        case 19:LABEL("Abnormal");break;
        case 20:LABEL("USB Debug");break;
        case 21:LABEL("Previous");break;
        case 22:LABEL("Next");break;
        case 24:LABEL("UI resources unavailable");break;
        case 25:LABEL("Retry");break;
        case 10:
            if(!atomic_load(&s->activation)) {
                uint32_t phase=atomic_load(&s->startup.phase);
                if(phase==FX_STARTUP_UNCHECKED){LABEL("Checking card");break;}
                if(phase==FX_STARTUP_IO_ERROR){LABEL("Card error");break;}
                if(phase==FX_STARTUP_ABNORMAL){LABEL("Abnormal shutdown");break;}
            }
            switch (atomic_load(&s->phase)) {
            case 0:
                LABEL("Ready");
                break;
            case 1:
                LABEL("Loading");
                break;
            case 2:
                LABEL("Active");
                break;
            case 3:
                LABEL("Card error");
                break;
            default:
                LABEL("Panic");
                break;
            }
            break;
        default:
            LABEL("Module");
            break;
        }
#undef LABEL
        *text = v;
        *bytes = n;
        return;
    }
    int32_t count = *(volatile int32_t *)(uintptr_t)UINT64_C(0xa02822824), index = count > id ? id : 0;
    const uint32_t *offsets = ((const uint32_t *const *)(uintptr_t)(*(
        volatile uintptr_t *)(uintptr_t)UINT64_C(0xa02f4ab38)))[language];
    const uint8_t *pool = ((const uint8_t *const *)(uintptr_t)(*(volatile uintptr_t *)(uintptr_t)UINT64_C(
        0xa02f4ab28)))[language];
    *text = (const uint16_t *)(const void *)(pool + (int32_t)offsets[index]);
    *bytes = offsets[index + 1] - offsets[index];
}
const uint32_t fx_native_font_index_metadata[]={0x46584649u,1u,
    offsetof(State,text_regions),offsetof(State,font_scope),sizeof(FxTextRegions),
    offsetof(FxTextRegions,root),offsetof(FxTextRegions,count),offsetof(FxTextRegion,begin),offsetof(FxTextRegion,end),
    offsetof(FxTextRegionBranch,child),offsetof(FxTextRegionBranch,prefix),offsetof(FxTextRegionBranch,bit),
    offsetof(FxNativeFont,error),sizeof(uintptr_t)*8u};
const FxNativeFontProfile fx_z9_font_profile={0x402e4364u,0x402e4378u,UINT64_C(0xa02eac0e8),0x40884508u,0x40883a38u,0x42271338u,170765u,0x4088f2a0u,0x4088ea2cu};
void fx_z9_font_enter(const void *request) {
    State *s=state();int enabled=s && (atomic_load(&s->activation)!=0u);
    fx_native_font_enter(enabled?&s->font_scope:0,&fx_z9_font_profile,enabled?&s->text_regions:0,request);
}
uint32_t fx_z9_font_line_metrics(uintptr_t engine,uintptr_t layout,int32_t dimensions[2]) {
    State *s=state();return fx_native_font_line_metrics(s?&s->font_scope:0,&fx_z9_font_profile,engine,layout,dimensions);
}
void fx_z9_font_leave(void) {
    State *s=state();fx_native_font_leave(s?&s->font_scope:0,&fx_z9_font_profile);
}
void fx_z9_menu_text(int32_t language,int32_t id,const uint16_t **text,uint32_t *bytes) {
    fx_z9_menu_text_impl(language,id,text,bytes);

}

extern uint64_t fx_z9_system_original(void *, uint32_t);
extern uint64_t fx_z9_navigate_original(uint32_t);
uint64_t fx_z9_menu_navigate(uint32_t page) {
    if(page>=FENESTRA && page<FX_NATIVE_EXTENSION_PAGE_LIMIT)
        return FN(0x403adbac,uint64_t (*)(uint32_t,uint32_t))(2u,page);
    return fx_z9_navigate_original(page);
}
uint64_t fx_z9_system_dispatch(void *message, uint32_t sync) {
    uint32_t event = message ? *(const uint32_t *)((const uint8_t *)message + 32u) : UINT32_MAX;
    State *s = state();
    if (event == 7u && s) {
        /* Original event is forwarded exactly once, even if a module/native
         * call hangs. Never remove the flag after the card cleanup window. */
        atomic_store(&s->startup.cleanup_window,1u);
        atomic_store_explicit(&s->startup.shutdown,1u,memory_order_release);
        for(uint32_t i=0;i<15u && !atomic_load_explicit(&s->startup.shutdown_done,memory_order_acquire);++i)
            (void)fx_z9_task_delay_ticks(33u);
        atomic_store(&s->startup.cleanup_window,0u);
    }
    uint64_t r = fx_z9_system_original(message, sync);
    /* Independently decoded C532 table: AppFirstStartComplete=19, NOT20. */
    if (event == 19u)
        install();
    if(event==24u && s)atomic_store(&s->startup.inspect_requested,1u);
    return r;
}
const uint32_t fx_z9_menu_metadata[] = {MAGIC,
                                        4u,
                                        sizeof(State),
                                        offsetof(State, magic),
                                        offsetof(State, phase),
                                        offsetof(State, activation),
                                        offsetof(State, error),
                                        offsetof(State, modules),
                                        offsetof(State, workers),
                                        offsetof(State, services),offsetof(State,management),offsetof(State,bus),offsetof(State,ui),offsetof(FxPlatformOwner,native_ui)};
const uint32_t fx_z9_startup_metadata[]={UINT32_C(0x46585339),1u,offsetof(State,startup),
    offsetof(FxStartupService,phase),offsetof(FxStartupService,desired),offsetof(FxStartupService,error),
    offsetof(FxStartupService,shutdown_done),offsetof(FxStartupService,flag_known),
    offsetof(State,startup_files)+offsetof(FxStartupFiles,native_error)};
const uint32_t fx_z9_ptp_metadata[]={UINT32_C(0x46585039),1u,offsetof(State,ptp),sizeof(FxPtp),
    offsetof(FxPtp,enabled),offsetof(FxPtp,generation),offsetof(FxPtp,executions)};
const uint32_t fx_z9_panic_metadata[]={UINT32_C(0x4658504e),1u,offsetof(State,panic),
    sizeof(PanicPage),offsetof(PanicPage,captured),offsetof(PanicPage,shown),offsetof(PanicPage,result),
    offsetof(PanicPage,report),offsetof(FxPanicReport,module_result),offsetof(FxPanicReport,cpu_faults),
    offsetof(FxPanicReport,request_sequence),offsetof(FxPanicReport,request_args),
    offsetof(PanicPage,display)+offsetof(FxNativeDisplay,frames),offsetof(State,fault_record)+4u};
