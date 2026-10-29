#include "fenestra_native_ui_editor.h"
extern const uint8_t __fx_core_code_begin[],__fx_core_code_end[];
#include "fenestra_native_ui_reader.h"
#include "fenestra_native_font.h"
#include "fenestra_native_apps.h"
/* C1.10 native menu extension, qualified only in the private QEMU experiment.
 * Immutable firmware code; UI metadata uses the normal firmware heap.
 * Startup follows original application/card events; all mutable state is owned.
 */
#include "fenestra_settings.h"
#include "fenestra_memory.h"
#include "fenestra_shared_accounts.h"
#include "fenestra_accounting_service.h"
#include "fenestra_c110_demo.h"
#include "fenestra_plugin_menu.h"
#include "fenestra_ptp.h"
#include "fenestra_c110_usb.h"
#include "fenestra_c110_rtos.h"
#include "fenestra_c110_observation.h"
#include "fenestra_c110_startup.h"
#include "fenestra_c110_display.h"
#include "fenestra_native_namespace.h"
#ifdef FX_PLATFORM_PROFILE
#include "fenestra_native_ui.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_ui_pager.h"
#include "fenestra_native_visibility.h"
#include "fenestra_native_title.h"
#include "fenestra_native_modules.h"
#include "fenestra_platform_native.h"
#include "fenestra_module_bus.h"
#include "fenestra_hooks.h"
#include "fenestra_native_mapping.h"
#define UI_PAGE_BASE FX_NATIVE_MODULE_PAGE_BASE
#ifdef FX_NATIVE_SCENE
#include "fenestra_c110_scene.h"
#endif
#ifdef FX_C110_CAPTURE_EXPERIMENT
#define UI_TEXT_BASE (3505u+FX_VIDEO_MAX_MODES)
#else
#define UI_TEXT_BASE 3507u
#endif
#define MANAGE_ID (UI_TEXT_BASE+FX_NATIVE_UI_TEXTS)
#define APPS_PREVIOUS_ID (MANAGE_ID+1u)
#define APPS_NEXT_ID (MANAGE_ID+2u)
#define APPS_EMPTY_ID (MANAGE_ID+3u)
#define APPS_UNAVAILABLE_ID (MANAGE_ID+4u)
#define APPS_RETRY_ID (MANAGE_ID+5u)
#define MODULE_TEXT_BASE (MANAGE_ID+6u)
#define MODULE_TEXT_STRIDE (FX_NATIVE_UI_TEXTS+2u)
#endif
#ifdef FX_C110_SENSOR_TRACE
#include "sensor_trace_native.h"
#endif
#ifdef FX_C110_EVENT_TRACE
#include "event_trace_native.h"
#endif
#ifdef FX_C110_CAPTURE_EXPERIMENT
#include "fenestra_c110_capture_menu.h"
#endif

#define SETUP_ID 708u
#define PAGE_ID FX_NATIVE_FENESTRA_PAGE
#define WONDER_PAGE_ID FX_NATIVE_WONDER_PAGE
#define STOCK_TEXT_COUNT 3479u
#define STOCK_LANGUAGE_COUNT 30u
#define LABEL_ID 3479u
#define ACTIVATE_ID 3480u
#define DEMO_ID 3481u
#define USB_ID 3482u
#define WRITE_ID 3483u
#define BACK_ID 3484u
#define FAULT_ID 3485u
#define WONDER_LABEL_ID 3486u
#define AUTO_ID 3487u
#define MODULE_ID 3488u
#define GUARD_ID 3489u
#define VALUE_BASE 3490u
#define PLUGIN_LABEL_BASE 3497u
#define PLUGIN_VALUE_BASE 3501u
#ifdef FX_C110_CAPTURE_EXPERIMENT
#define LEGACY_TEXT_COUNT (26u+FX_VIDEO_MAX_MODES)
#else
#define LEGACY_TEXT_COUNT 26u
#endif
#ifdef FX_PLATFORM_PROFILE
#define CUSTOM_TEXT_COUNT (MODULE_TEXT_BASE-STOCK_TEXT_COUNT)
#else
#define CUSTOM_TEXT_COUNT LEGACY_TEXT_COUNT
#endif
#ifdef FX_C110_FAULT_TEST
#define ROW_COUNT 6u
#else
#define ROW_COUNT 5u
#endif
#ifdef FX_PLATFORM_PROFILE
#undef ROW_COUNT
#ifdef FX_C110_FAULT_TEST
#define ROW_COUNT 7u
#else
#define ROW_COUNT 6u
#endif
#endif
#define MAGIC 0x46584331u
#define FN(a,t) ((t)(uintptr_t)(a))

typedef FxNativeRow Row;
typedef FxNativeEvent Event;
typedef FxNativeDescriptor Descriptor;
typedef FxNativePage Page;
typedef struct {
    Descriptor setup;
    uint32_t magic,installed,page_requests,text_requests,activation_requests;
    uint32_t last_action;
    FxSettings settings;
    Row setup_rows[55];
    uint64_t setup_events[55];
    Event entry_events[2][18];
    Page page;
    Descriptor descriptor;
    uint8_t header[272];
    Row rows[9];
    uint64_t events[9],callbacks[5];
    Event row_events[9][18];
    uint32_t notice_inserted;
    _Atomic(FxNativeTextBank *) text_bank;
    _Atomic(FxC110Demo *) demo;
    Page wonder_page;
    Descriptor wonder_descriptor;
    uint8_t wonder_header[272];
    Row wonder_rows[8];
    uint64_t wonder_events[8];
    Event wonder_row_events[4][18];
    Row plugin_rows[4],wonder_back;
    uint64_t wonder_back_event;
    Event plugin_events[4][18];
    uint16_t plugin_labels[4][32],plugin_values[4][16];
#ifdef FX_PLATFORM_PROFILE
    FxNativeUi **ui;uint32_t ui_count,ui_capacity;
    _Atomic(FxNativeApps *) apps;
    _Atomic(FxNativeModules *) management;
    _Atomic uint32_t management_accounted,ui_vector_accounted;
    _Atomic(FxModuleBus *) bus;
    FxHookRegistry hooks;
    FxNativeMappingRegistry mappings;
    FxNativeUiDispatch ui_dispatch;FxNativeUiSpace ui_space;
#endif
    FxPtp ptp;
    FxC110Usb usb;
    FxC110Startup startup;
    FxC110Rtos rtos;
    FxC110Observation observation;
#ifdef FX_C110_SENSOR_TRACE
    _Atomic(FxSensorTrace *) sensor_trace;
    _Atomic uint32_t sensor_trace_created;
#endif
#ifdef FX_C110_EVENT_TRACE
    _Atomic(FxEventTrace *) event_trace;
    _Atomic uint32_t event_trace_created;
#endif
#ifdef FX_C110_CAPTURE_EXPERIMENT
    _Atomic(FxC110CaptureMenu *) capture;
    FxTextRegion capture_text_region;
#endif
    FxNativeFont font_scope;
    FxTextRegions text_regions;FxTextRegion state_text_region,image_text_region;
#ifdef FX_PLATFORM_PROFILE
    FxSharedAccounts accounting;
    FxCapacityRegistry capacity_registry;FxNativeCapacityDomains capacity_domains;
    FxNativeApps apps_storage;
#endif

} State;
_Static_assert(sizeof(Row)==44,"C1.10 native row");
_Static_assert(sizeof(Event)==32,"C1.10 native event");
_Static_assert(sizeof(Descriptor)==64,"C1.10 native descriptor");
_Static_assert(sizeof(Page)==96,"C1.10 native page");
_Static_assert(sizeof(FxSettings)<=32,"Dormant settings stub");

static Page *stock_page(uint32_t group,uint32_t id) {
    if (group>6u) return 0;
    uintptr_t base=((volatile uintptr_t *)(uintptr_t)UINT64_C(0xa034dfc20))[group];
    if (!base) return 0;
    return (Page *)(base+(id&65535u)*sizeof(Page));
}
static void *map_allocate(void *context,uint32_t bytes){(void)context;return FN(0x40f41084,void *(*)(uint32_t,uint32_t))(bytes,0u);}
static void map_release(void *context,void *memory){(void)context;FN(0x40f410c4,void (*)(void *))(memory);}
static int text_ensure(State *s,uint32_t required) {
    FxNativeTextBank *old=atomic_load_explicit(&s->text_bank,memory_order_acquire),*bank=0;
    if(fx_native_text_bank_grow((FxIndexMemory){0,map_allocate,map_release},old,required,&bank)!=FX_OK)return 0;
    if(bank==old)return 1;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa030fafa8)=(uintptr_t)bank->fonts;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa030fafb8)=(uintptr_t)bank->attributes;
    __asm__ volatile("dmb ish":::"memory");
    atomic_store_explicit(&s->text_bank,bank,memory_order_seq_cst);
#ifdef FX_PLATFORM_PROFILE
    fx_retained_account(fx_shared_text_account(&s->accounting),&bank->accounted,bank->bytes);
#endif
    return 1;
}
static State *state(void) {
    Page *p=stock_page(2u,SETUP_ID);
    if ((uintptr_t)p<UINT64_C(0xa00000000) ||
        (uintptr_t)p>UINT64_C(0xa40000000)-sizeof(Page)) return 0;
    if (!p || p->descriptor==UINT64_C(0xa028e70d0)) return 0;
    uintptr_t a=(uintptr_t)p->descriptor;
    if ((a&7u) || a<UINT64_C(0xa00000000) || a>UINT64_C(0xa40000000)-sizeof(State)) return 0;
    __asm__ volatile("dmb ish":::"memory");
    State *s=(State *)a;
    return s->magic==MAGIC && s->installed?s:0;
}
static uint32_t noop(Page *p) { (void)p; return 1u; }
void fx_c110_menu_observer_fault(FxResult error) {
    State *s=state();if(!s)return;
    fx_c110_observation_cancel(FX_OBS_SOURCE_ERROR);
    fx_c110_demo_observer_fault(atomic_load_explicit(&s->demo,memory_order_acquire),error);
}
#ifdef FX_C110_CAPTURE_EXPERIMENT
FxC110CaptureMenu *fx_c110_capture_menu(void) {State *s=state();return s?atomic_load_explicit(&s->capture,memory_order_acquire):0;}
FxC110CaptureMenu *fx_c110_capture_create(void) {
    State *s=state();if(!s || !fx_c110_menu_ptp_active())return 0;
    FxC110CaptureMenu *current=atomic_load_explicit(&s->capture,memory_order_acquire);
    if(!current) {
        FxAllocationObserver observer={0};
#ifdef FX_PLATFORM_PROFILE
        observer=fx_account_observer(fx_shared_account_row(&s->accounting.state,"video.adapter"));
#endif
        FxC110CaptureMenu *created=FN(UINT64_C(0x40f41084),void *(*)(uint32_t,uint32_t))((uint32_t)sizeof(*created),0u);
        if(!created)return 0;memset(created,0,sizeof(*created));
        created->allocation_observer=observer;
        fx_allocation_observe(observer,(uint32_t)sizeof(*created),FX_ALLOC_REQUESTED);
        if(!atomic_compare_exchange_strong_explicit(&s->capture,&current,created,memory_order_release,memory_order_acquire)) {
            FN(UINT64_C(0x40f410c4),void (*)(void *))(created);
            fx_allocation_observe(observer,(uint32_t)sizeof(*created),FX_ALLOC_FREE_PENDING);
        } else {
            fx_allocation_observe(observer,(uint32_t)sizeof(*created),FX_ALLOC_PUBLISHED);
            current=created;
        }
    }
    /* The generic video UI owns a separate retained text allocation. Register
     * only the retained winner, never an unpublished CAS loser that is freed. */
    return fx_native_text_regions_add(&s->text_regions,&s->capture_text_region,current,sizeof(*current))==FX_OK?current:0;
}
#endif
static uint32_t page_opened(Page *p);
#ifdef FX_PLATFORM_PROFILE
static FxNativeUi *module_ui(uint32_t index,int create);
uint64_t fx_c110_menu_navigate(uint32_t page);
uint32_t fx_native_module_loaded(const char *id){State *s=state();return s?fx_c110_module_loaded(s->demo,id):0u;}
static const FxNativeVisibilityProfile ui_visibility;
static const FxNativeLayoutProfile ui_layout;
static const FxNativeEvent ui_editor_events[]={
    {0x2b4u,0x4052e158u,0x0u,0u},
    {0x2b5u,0x4052e15cu,0x0u,0u},
    {0x2b6u,0x4052e160u,0x0u,0u},
    {0x2b7u,0x4052e164u,0x0u,0u},
    {0x2bcu,0x4052e1acu,0x0u,0u},
    {0x2ceu,0x4052e170u,0x0u,0u},
    {0x2cfu,0x4052e174u,0x0u,0u},
    {0x2d0u,0x4052e174u,0x403be3d4u,0u},
    {0x2d1u,0x4052e170u,0x403be3d4u,0u},
    {0x2dfu,0x4052e1acu,0x0u,0u},
    {0x2e2u,0x4052e1b0u,0x0u,0u},
    {0x334u,0x4052e1b8u,0x0u,0u},
    {0x336u,0x4052e1bcu,0x0u,0u},
};
static const FxNativeUiEditorProfile ui_editor_profile={
    (const FxNativePage *)(uintptr_t)UINT64_C(0xa02927188),ui_editor_events,
    13u,736u,687u,824u,
    0x40641f9cu,0x40644098u,0x405341f4u,0x403bea80u,0x403e8e8cu};
static const FxNativeUiReaderProfile ui_reader_profile={{0x403e8e8cu,0x40400330u,0x40337eecu,0x40401060u,0x40333038u,0x40330b98u,0x4fb7u},0x4032a618u,0x4032a4f4u,720u,480u};
static const FxNativeTitleProfile ui_title={0x403e8e8cu,0x40400330u,0x40337eecu,0x40401060u,0x40333038u,0x40330b98u,0x4fb7u};
typedef struct {FxPlatformOwner *owner;FxNativeUi *ui;uint32_t revision,destination;} UiRefresh;
static FxResult refresh_ui_values(void *context,void *current,uint32_t *rows_changed) {
    UiRefresh *request=context;
    Page *page=current;uint32_t id=page->identity>>16;
    FxNativeUi *ui=fx_native_ui_find_page(id);
    if(!ui || ui->owner!=request->owner)return FX_EMPTY;
    uint32_t changed=0u;FxResult r=fx_native_ui_retirement_target(ui,id,&request->destination);
    if(r==FX_OK)return FX_EMPTY; /* Navigate after releasing the GUI mutex. */
    if(r!=FX_EMPTY)return r;
    r=fx_native_ui_prepare_navigation(ui,(FxNativePage *)page,&request->destination);
    if(r==FX_OK)return FX_EMPTY;
    if(r!=FX_EMPTY)return r;
    r=fx_native_ui_trace(ui,1u,0u,fx_native_ui_recover_values(ui,id,&changed));
    if(r==FX_OK) {
        request->ui=ui;request->revision=ui->revision;
        const FxNativeDescriptor *d=(const FxNativeDescriptor *)page->descriptor;
        const FxNativeRow *rows=(const FxNativeRow *)d->rows;
        if(changed&FX_NATIVE_UI_TITLE) {
            r=fx_native_ui_trace(ui,2u,0u,fx_native_title_prepare(&ui_title,(FxNativePage *)page));
            if(r!=FX_OK)return r;
            *rows_changed|=FX_NATIVE_UI_TITLE;
        }
        if(changed&FX_NATIVE_UI_STRUCTURE) {
            for(uint32_t i=0;i<d->count;++i) {
                r=fx_native_ui_trace(ui,3u,0u,fx_native_row_visibility(&ui_visibility,(FxNativePage *)page,rows[i].word[6],
                    fx_native_ui_row_visible(ui,id,rows[i].word[6])));
                if(r!=FX_OK)return r;
                r=fx_native_ui_trace(ui,4u,0u,fx_native_row_arrow(&ui_visibility,(FxNativePage *)page,rows[i].word[6],
                    fx_native_ui_row_visible(ui,id,rows[i].word[6]) && fx_native_ui_row_submenu(ui,rows[i].word[6])));
                if(r!=FX_OK)return r;
                int enabled=1;uint32_t value=fx_native_ui_value(ui,rows[i].word[6],&enabled);
                r=fx_native_ui_trace(ui,5u,0u,fx_native_row_value_visibility(&ui_visibility,(FxNativePage *)page,rows[i].word[6],
                    value && fx_native_ui_row_visible(ui,id,rows[i].word[6])));
                if(r!=FX_OK)return r;
            }
            r=fx_native_ui_trace(ui,6u,0u,fx_native_ui_order_page(ui,(FxNativePage *)page));
            if(r==FX_OK)r=fx_native_ui_trace(ui,7u,0u,fx_native_row_reflow(&ui_layout,(FxNativePage *)page));
            if(r!=FX_OK)return r;
        }
        *rows_changed|=fx_native_ui_repaint_rows(ui,(FxNativePage *)page,changed)|(changed&FX_NATIVE_UI_STRUCTURE);
    }return r;
}
static FxResult ui_presented(void *context) {
    UiRefresh *request=context;
    return fx_native_ui_presented(request->ui,request->revision);
}
FxResult fx_native_ui_flush_owner(FxPlatformOwner *owner) {
    UiRefresh request={.owner=owner};
    FxResult r=fx_c110_display_refresh_page(&request,refresh_ui_values,ui_presented);
    if(request.destination) {
        (void)fx_c110_menu_navigate(request.destination);
        fx_native_ui_refresh_owner(owner);return FX_OK;
    }
    return r;
}
static FxResult apps_prepare_navigation(void *context,void *page,uint32_t *changed) {
    (void)changed;uint32_t *destination=context;
    FxResult r=fx_native_apps_prepare_navigation(fx_native_apps_domain(),page,destination);
    return r==FX_OK?FX_EMPTY:r; /* Only prepare; the source remains untouched. */
}
FxResult fx_native_apps_flush_native(void) {
    uint32_t destination=0u;
    FxResult r=fx_c110_display_refresh_page(&destination,apps_prepare_navigation,0);
    if(destination){(void)fx_c110_menu_navigate(destination);return FX_OK;}
    return r;
}
FxNativeUiDispatch *fx_native_ui_dispatch_domain(void){State *s=state();return s?&s->ui_dispatch:0;}
const uint32_t fx_native_ui_dispatch_metadata[10]={0x46585544u,2u,offsetof(State,ui_dispatch),sizeof(FxNativeUiDispatch),
    offsetof(FxNativeUiDispatch,task),offsetof(FxNativeUiDispatch,pending),offsetof(FxNativeUiDispatch,running),
    offsetof(FxNativeUiDispatch,submitted),offsetof(FxNativeUiDispatch,completed),offsetof(FxNativeUiDispatch,last_task)};
static void *ui_allocate(uint32_t bytes){return FN(UINT64_C(0x40f41084),void *(*)(uint32_t,uint32_t))(bytes,0u);}
void fx_native_module_fault(FxPlatformOwner *owner,FxResult error) {
    (void)owner;State *s=state();if(s)fx_c110_demo_observer_fault(atomic_load_explicit(&s->demo,memory_order_acquire),error);
}
FxHookRegistry *fx_native_hooks(void) {State *s=state();return s?&s->hooks:0;}
FxAccountState *fx_native_shared_accounts(void){State *s=state();return s && atomic_load_explicit(&s->accounting.ready,memory_order_acquire)?&s->accounting.state:0;}
FxCapacityRegistry *fx_native_capacity_registry(void){State *s=state();return s && atomic_load_explicit(&s->capacity_registry.ready,memory_order_acquire)?&s->capacity_registry:0;}
FxNativeMappingRegistry *fx_native_mappings(void) {State *s=state();return s?&s->mappings:0;}
FxModuleBus *fx_native_module_bus(void) {
    State *s=state();if(!s)return 0;
    FxModuleBus *b=atomic_load_explicit(&s->bus,memory_order_acquire);if(b)return b;
    b=ui_allocate((uint32_t)sizeof(*b));if(!b)return 0;memset(b,0,sizeof(*b));
    if(atomic_load_explicit(&s->accounting.ready,memory_order_acquire))b->metadata=fx_account_observer(&s->accounting.bus);
    b->owner_queue=fx_account_bus_queues;fx_allocation_observe(b->metadata,(uint32_t)sizeof(*b),FX_ALLOC_REQUESTED);
    if(fx_module_bus_init(b,(FxIndexMemory){0,map_allocate,map_release})!=FX_OK){FxAllocationObserver observer=b->metadata;map_release(0,b);fx_allocation_observe(observer,(uint32_t)sizeof(*b),FX_ALLOC_FREE_PENDING);return 0;}
    FxModuleBus *zero=0;
    if(!atomic_compare_exchange_strong_explicit(&s->bus,&zero,b,memory_order_release,memory_order_acquire)){FxAllocationObserver observer=b->metadata;map_release(0,b);fx_allocation_observe(observer,(uint32_t)sizeof(*b),FX_ALLOC_FREE_PENDING);return zero;}
    fx_allocation_observe(b->metadata,(uint32_t)sizeof(*b),FX_ALLOC_PUBLISHED);
    return b;
}
static const FxNativeLayoutProfile ui_layout={
    (uintptr_t)FN(0x403e8e8c,void *(*)(void)),
    (uintptr_t)FN(0x403bc3c8,uint32_t (*)(void *,uint32_t)),
    (uintptr_t)FN(0x403bc4dc,void (*)(void *,uint32_t,uint32_t)),
    (uintptr_t)FN(0x403bc630,void (*)(void *,uint32_t,uint32_t)),
    (uintptr_t)FN(0x403bc444,void (*)(void *)),
    (uintptr_t)FN(0x403badb4,void (*)(void *)),
    (uintptr_t)FN(0x403bb078,void (*)(void *)),
    (uintptr_t)FN(0x403bc924,void (*)(void *)),
    (uintptr_t)FN(0x403bb2cc,void (*)(void *))};
static const FxNativeVisibilityProfile ui_visibility={
    (uintptr_t)FN(UINT64_C(0x403e8e8c),void *(*)(void)),
    (uintptr_t)FN(UINT64_C(0x40400330),void *(*)(uint32_t,void *)),
    (uintptr_t)FN(UINT64_C(0x404010b0),void (*)(void *,uint32_t)),
    UINT64_C(0x4032b258),(uintptr_t)FN(0x404012d0,void *(*)(void *,uint32_t)),0x148u,0x4fb2u,0x4fb1u};
static uint32_t ui_opened(Page *p) {
    uint32_t id=p->identity>>16;FxNativeUi *u=fx_native_ui_find_page(id);if(!u || !fx_native_ui_page_matches(u,(FxNativePage *)p))return 1u;
    if(u)fx_native_ui_capture_task();
    if(u)(void)fx_native_ui_open(u,id);
    if(u)(void)fx_native_backdrop_open((FxNativePage *)p);
    Descriptor *d=(Descriptor *)(uintptr_t)p->descriptor;Row *rows=(Row *)(uintptr_t)d->rows;
    for(uint32_t i=0;i<d->count;++i) {
        int visible=!u || fx_native_ui_row_visible(u,id,rows[i].word[6]);
        (void)fx_native_row_visibility(&ui_visibility,(FxNativePage *)p,rows[i].word[6],visible);
        (void)fx_native_row_arrow(&ui_visibility,(FxNativePage *)p,rows[i].word[6],
            visible && fx_native_ui_row_submenu(u,rows[i].word[6]));
        int enabled=1;uint32_t value=fx_native_ui_value(u,rows[i].word[6],&enabled);
        (void)fx_native_row_value_visibility(&ui_visibility,(FxNativePage *)p,rows[i].word[6],visible && value);
        /* Hidden slots still own native value children. Resolve their empty
         * resources at construction, not only after a later revision. */
        (void)fx_c110_display_prepare_text(p,rows[i].word[6],rows[i].word[0]);
    }
    if(u)(void)fx_c110_display_prepare_layout(p,&ui_layout);
    if(u)fx_native_ui_refresh_owner(u->owner);
    return 1u;
}
static uint32_t ui_closed(Page *p) {
    fx_native_backdrop_close((FxNativePage *)p);
    uint32_t id=p->identity>>16;{
#ifdef FX_NATIVE_SCENE
        FxNativeUi *scene_ui=fx_native_ui_find_page(id);
        if(scene_ui)fx_c110_scene_closed(scene_ui->owner,id);
#endif
        FxNativeUi *u=fx_native_ui_find_page(id);if(u && fx_native_ui_page_matches(u,(FxNativePage *)p))(void)fx_native_ui_close(u,id);
    }return 1u;
}
static FxNativeUi *module_ui(uint32_t index,int create) {
    (void)create;State *s=state();if(!s)return 0;
    if(index==UINT32_MAX) {
        if(!s->management)return 0;if(fx_c110_modules_ready(s->demo))fx_module_manager_ready(&s->management->manager);
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
    FxPlatformOwner *owner=fx_c110_module_platform(s->demo,index);
    if(!owner || (owner->phase!=FX_OWNER_RUNNING && owner->phase!=FX_OWNER_FINISHED))return 0;
    FxNativeUi *cached=(FxNativeUi *)atomic_load_explicit(&owner->native_ui,memory_order_acquire);
    if(cached) {
        uint32_t pinned=0u;for(uint32_t i=0u;i<FX_NATIVE_UI_PAGES;++i)pinned|=cached->pinned[i];
        if(!pinned){cached->profile.page=parent;cached->profile.back=parent->identity>>16;}
        ui_pager_if_needed(s,cached);return cached;
    }
    if(!s->ui) {
        uint32_t count=fx_c110_module_capacity(s->demo);if(count>FX_NATIVE_UI_OWNERS)count=FX_NATIVE_UI_OWNERS;
        if(!count)return 0;s->ui=ui_allocate(count*(uint32_t)sizeof(*s->ui));if(!s->ui)return 0;
        memset(s->ui,0,count*sizeof(*s->ui));s->ui_capacity=count;
        fx_retained_account(fx_shared_ui_account(&s->accounting),&s->ui_vector_accounted,count*(uint32_t)sizeof(*s->ui));
    }
    if(ui_space_ready(s)!=FX_OK)return 0;
    uint32_t slot=s->ui_count;if(slot>=s->ui_capacity ||
        !text_ensure(s,MODULE_TEXT_BASE+(slot+1u)*MODULE_TEXT_STRIDE))return 0;
    FxAllocationObserver observer=fx_platform_account_observer(owner,"ui.native");
    FxNativeUi *u=ui_allocate((uint32_t)sizeof(*u));if(!u)return 0;memset(u,0,sizeof(*u));
    fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_REQUESTED);
    static const FxNativeDialogStyle dialog={
        (const FxNativeDescriptor *)(uintptr_t)UINT64_C(0xa028d2b30),
        (const FxNativeRow *)(uintptr_t)UINT64_C(0x43d3075c),340u,2u};
    FxNativeMenuProfile profile={parent,(const FxNativeRow *)&s->setup_rows[52],
        (const FxNativeEvent *)(uintptr_t)UINT64_C(0x43f77ce8),0x3fdu,0x2b6u,{0x2b7u,0x2bcu,0x2d0u,0x2dfu},0x749u,parent->identity>>16};
    uintptr_t callbacks[5]={(uintptr_t)ui_opened,(uintptr_t)ui_closed,UINT64_C(0x403be9b4),(uintptr_t)noop,(uintptr_t)noop};
    uint32_t controls=0u,primary_pages=FX_NATIVE_UI_PAGES;int visible=0;
    if(fx_ui_presence(owner,&controls,&visible)==FX_OK && controls>FX_UI_CONTROLS)primary_pages=2u;
    uint32_t physical=0u;
    if(fx_native_ui_space_take(&s->ui_space,primary_pages,&physical)!=FX_OK){map_release(0,u);fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_FREE_PENDING);return 0;}
    if(fx_native_ui_init(u,owner,&profile,ui_allocate,physical,
        MODULE_TEXT_BASE+slot*MODULE_TEXT_STRIDE,0x8000u+slot*64u,fx_native_ui_actions,fx_native_ui_options,callbacks)!=FX_OK){fx_native_ui_space_rollback(&s->ui_space,physical,primary_pages);map_release(0,u);fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_FREE_PENDING);return 0;}
    if(primary_pages==2u)u->picker_page=physical+1u;
    u->dialog=&dialog;u->editor_profile=&ui_editor_profile;u->reader_profile=&ui_reader_profile;
    u->request_refresh=fx_native_ui_refresh_owner;
    if(fx_ui_dispatch_bind_page(&s->ui_dispatch,u)!=FX_OK){fx_native_ui_space_rollback(&s->ui_space,physical,primary_pages);map_release(0,u);fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_FREE_PENDING);return 0;}
    fx_native_ui_identity(u,fx_c110_module_identity(s->demo,index));
    s->ui[slot]=u;s->ui_count=slot+1u;
    atomic_store_explicit(&owner->native_ui,(uintptr_t)u,memory_order_release);
    fx_allocation_observe(observer,(uint32_t)sizeof(*u),FX_ALLOC_PUBLISHED);ui_pager_if_needed(s,u);return u;
}

static uint32_t page_closed(Page *);

FxNativeApps *fx_native_apps_domain(void){State *s=state();return s?s->apps:0;}
static uint32_t apps_count(void *context){State *s=context;return fx_c110_modules_ready(s->demo)?fx_c110_module_count(s->demo):0u;}
static FxResult apps_entry(void *context,uint32_t index,const FxNativePage *parent,int selected,FxNativeAppEntry *entry) {
    State *s=context;FxPlatformOwner *owner=fx_c110_module_platform(s->demo,index);if(!owner)return FX_EMPTY;
    if(owner->phase!=FX_OWNER_RUNNING && owner->phase!=FX_OWNER_FINISHED)return FX_EMPTY;
    uint32_t controls=0u;int visible=0;FxResult present=fx_ui_presence(owner,&controls,&visible);if(present!=FX_OK)return present;
    if(controls && !visible)return FX_EMPTY;
    char label[32];uint32_t value;int enabled=0;
    int legacy=!controls && fx_c110_module_menu(s->demo,index,label,&value,&enabled);
    if(!controls && !legacy)return FX_EMPTY;
    if(!parent){memset(entry,0,sizeof(*entry));return FX_OK;}
    FxNativeUi *u=instance_ui(s,index,parent);if(!u)return FX_BUDGET;
    if(selected && controls && !fx_native_ui_page(u,u->page_base))return FX_BUDGET;
    if(selected && controls && !fx_native_ui_root_visible(u))return FX_EMPTY;
    *entry=(FxNativeAppEntry){u->text_base+FX_NATIVE_UI_TEXTS,legacy?u->text_base+FX_NATIVE_UI_TEXTS+1u:0u,
        legacy?(uint32_t)u->profile.stay:u->page_base,
        !atomic_load(&s->startup.shutdown) && (owner->phase==FX_OWNER_RUNNING || (!legacy && owner->phase==FX_OWNER_FINISHED)),&u->app_links,u->observer};
    return FX_OK;
}
static FxResult apps_action(void *context,uint32_t index){State *s=context;return fx_c110_module_action(s->demo,index);}
static FxNativePage *apps_page(State *s,uint32_t page) {
    if(!s->apps) {
        FxNativeApps *apps=&s->apps_storage;
    FxNativeMenuProfile profile={(FxNativePage *)stock_page(2u,SETUP_ID),(const FxNativeRow *)&s->setup_rows[52],
        (const FxNativeEvent *)(uintptr_t)UINT64_C(0x43f77ce8),0x3fdu,0x2b6u,{0x2b7u,0x2bcu,0x2d0u,0x2dfu},0x749u,SETUP_ID};
    uintptr_t callbacks[5]={(uintptr_t)page_opened,(uintptr_t)page_closed,UINT64_C(0x403be9b4),(uintptr_t)noop,(uintptr_t)noop};
        if(fx_native_apps_init(apps,&profile,callbacks,s,ui_allocate,apps_count,apps_entry,apps_action,
            WONDER_LABEL_ID,APPS_PREVIOUS_ID,APPS_NEXT_ID,APPS_EMPTY_ID,BACK_ID,
            APPS_UNAVAILABLE_ID,APPS_RETRY_ID)!=FX_OK)return 0;
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
    if(fx_native_ui_pager_text(&s->ui_space,id,text,bytes))return 1;
    if(id<MODULE_TEXT_BASE)return 0;
    uint32_t slot=(id-MODULE_TEXT_BASE)/MODULE_TEXT_STRIDE,offset=(id-MODULE_TEXT_BASE)%MODULE_TEXT_STRIDE;
    FxNativeUi *u=module_ui(slot,0);static const uint16_t empty[]=u"";*text=empty;*bytes=0u;
    if(!u)return 1;
    if(offset<FX_NATIVE_UI_TEXTS){(void)fx_native_ui_text(u,id,text,bytes);return 1;}
    uint32_t controls=0u;int visible=0;(void)fx_ui_presence(u->owner,&controls,&visible);
    if(!controls) {
        char label[32];uint32_t value=0u;int enabled=0;
        if(!fx_c110_module_menu(s->demo,u->owner->owner-1u,label,&value,&enabled))return 1;
        if(offset==FX_NATIVE_UI_TEXTS) {
            uint32_t n=0u;while(n<31u && label[n]){u->labels[0][n]=(uint16_t)(uint8_t)label[n];++n;}u->labels[0][n]=0u;*text=u->labels[0];*bytes=n*2u;
        } else {
            uint16_t reverse[10];uint32_t n=0u;do{reverse[n++]=(uint16_t)('0'+value%10u);value/=10u;}while(value && n<10u);
            for(uint32_t i=0u;i<n;++i)u->values[0][i]=reverse[n-1u-i];u->values[0][n]=0u;*text=u->values[0];*bytes=n*2u;
        }
        return 1;
    }
    if(offset==FX_NATIVE_UI_TEXTS) {
        *text=u->contribution_title;uint32_t n=0u;while(n<FX_UI_TEXT && (*text)[n])++n;*bytes=n*2u;
    }
    return 1;
}

static uint32_t manage_modules(void *p) {
    (void)p;State *s=state();if(!s || atomic_load(&s->startup.shutdown))return 0u;
    if(!s->management) {
        FxNativeModules *m=ui_allocate((uint32_t)sizeof(*m));if(!m)return 0u;
        FxNativeMenuProfile profile={(FxNativePage *)&s->page,(const FxNativeRow *)&s->setup_rows[52],
            (const FxNativeEvent *)(uintptr_t)UINT64_C(0x43f77ce8),0x3fdu,0x2b6u,
            {0x2b7u,0x2bcu,0x2d0u,0x2dfu},0x749u,PAGE_ID};
        uintptr_t callbacks[5]={(uintptr_t)ui_opened,(uintptr_t)ui_closed,UINT64_C(0x403be9b4),(uintptr_t)noop,(uintptr_t)noop};
        uint32_t phase=atomic_load(&s->startup.phase);
        if(fx_native_modules_init(m,fx_platform_target(),&profile,ui_allocate,
            UI_TEXT_BASE,0x7d00u,callbacks,
            phase==FX_STARTUP_ARMED || phase==FX_STARTUP_RUNNING)!=FX_OK){map_release(0,m);return 0u;}
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
#endif
static FxResult start_core(void *context,uint32_t selection) {
    State *s=context;
#if defined(FX_PLATFORM_PROFILE) && defined(FX_C110_FAULT_TEST)
    if(s->management && fx_module_manager_freeze(&s->management->manager)!=FX_OK)return FX_BUSY;
    /* The retained demo bridge predates generic workers. Respect its normal
     * module filename state too; never interpret a disabled file as missing
     * mandatory code and Panic during activation. */
    int32_t fd=FN(UINT64_C(0x402e883c),int32_t (*)(const char *,uint32_t,uint32_t))
        ("A:\\FENESTRA\\demo.wlm.disabled",0u,0u);
    FxFileObserver observer=fx_capacity_file_observer(&s->capacity_registry);FxCapacityClaim claim=fx_file_observe_open(observer,fd);
    if(fd>=0) {
        uint32_t closed=FN(UINT64_C(0x402e889c),uint32_t (*)(int32_t))(fd);
        fx_file_observe_close(observer,&claim,closed);if(closed)return FX_CLOSE;
        selection&=~FX_STARTUP_DEMO;
    } else if((uint32_t)fd!=UINT32_C(0x80000010))return FX_IO;
#endif
#if defined(FX_PLATFORM_PROFILE) && !defined(FX_C110_FAULT_TEST)
    if(s->management && fx_module_manager_freeze(&s->management->manager)!=FX_OK)return FX_BUSY;
#endif
    FxC110Demo *d=atomic_load_explicit(&s->demo,memory_order_acquire);
    if(d)return fx_c110_demo_request(d,FX_C110_RETRY_CARD);
#ifdef FX_PLATFORM_PROFILE
    if(!atomic_load_explicit(&s->accounting.ready,memory_order_acquire)) {
        (void)fx_shared_accounts_init(&s->accounting,(FxIndexMemory){0,map_allocate,map_release},(uint32_t)sizeof(*s),(uint32_t)sizeof(s->ui_dispatch.tasks));
        fx_linux_account(&s->usb,fx_account_observer(fx_shared_account_row(&s->accounting.state,"debug.linux")));
        fx_linux_account_packets(&s->usb,(FxIndexMemory){0,map_allocate,map_release},
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
        atomic_fetch_or(&s->accounting.state.flags,FX_ACCOUNT_SEEDED);
    }
#endif
    FxResult result=fx_c110_demo_start(&s->settings,&d,&s->page,&s->wonder_page,selection);
    atomic_store_explicit(&s->demo,d,memory_order_release);
    return result;
}
static uint32_t core_phase(void *context) { return fx_c110_demo_phase(((State *)context)->demo); }
static int stop_core(void *context) {
    State *s=context;
    int core=fx_c110_demo_shutdown(s->demo);
#ifdef FX_C110_CAPTURE_EXPERIMENT
    fx_c110_capture_stop();
#endif
    fx_c110_observation_cancel(FX_OBS_LOCAL_REVOKE);
    fx_ptp_enable(&s->ptp,0);fx_c110_usb_revoke();
    return core && fx_c110_usb_quiesced() && fx_c110_rtos_quiesced() && fx_c110_observation_quiesced()
#ifdef FX_C110_CAPTURE_EXPERIMENT
        && fx_c110_capture_stopped()
#endif
        ;
}
static void refresh_startup(void *context) {
    State *s=context;
    for(uint32_t i=0;i<s->descriptor.count;++i)
        (void)fx_c110_display_refresh_text(&s->page,s->rows[i].word[6],s->rows[i].word[0]);
    for(uint32_t i=0;i<s->wonder_descriptor.count;++i)
        (void)fx_c110_display_refresh_text(&s->wonder_page,s->wonder_rows[i].word[6],s->wonder_rows[i].word[0]);
}
static uint32_t startup_action(uint32_t command) {
    State *s=state();if(!s)return 0u;
    FxResult r=fx_c110_startup_start(&s->startup);
    if(r==FX_OK)r=fx_c110_startup_request(&s->startup,command);
    s->last_action=(uint32_t)r;
    return 1u;
}
static uint32_t activate(void *context) {
    (void)context;State *s=state();if(s)++s->activation_requests;
    return startup_action(FX_BOOT_ACTIVATE);
}
static uint32_t auto_toggle(void *context) {(void)context;return startup_action(FX_BOOT_AUTO_TOGGLE);}
#ifdef FX_C110_FAULT_TEST
static uint32_t module_toggle(void *context) {(void)context;return startup_action(FX_BOOT_DEMO_TOGGLE);}
#endif
static uint32_t guard_clear(void *context) {(void)context;return startup_action(FX_BOOT_CLEAR);}
#ifdef FX_C110_FAULT_TEST
static uint32_t demo(void *context) {
    (void)context;State *s=state();
    if(s) s->last_action=(uint32_t)fx_c110_demo_request(s->demo,FX_C110_PLAY);
    return s?1u:0u;
}
#endif
#ifdef FX_C110_FAULT_TEST
static uint32_t fault_test(void *context) {
    (void)context;State *s=state();
    if(s) s->last_action=(uint32_t)fx_c110_demo_request(s->demo,FX_C110_TEST_FAULT);
    return s?1u:0u;
}
#endif
static uint32_t page_closed(Page *p) {
    (void)p;State *s=state();
    if(s) fx_c110_demo_page_closed(s->demo);
    return 1u;
}
static uint32_t plugin_action(uint32_t index) {
#ifdef FX_PLATFORM_PROFILE
    FxNativeUi *u=module_ui(index,0);if(u && u->count)return 1u;
#endif
    State *s=state();if(s)s->last_action=(uint32_t)fx_c110_module_action(s->demo,index);
    return s?1u:0u;
}
static uint32_t plugin0(void *p){(void)p;return plugin_action(0u);}
static uint32_t plugin1(void *p){(void)p;return plugin_action(1u);}
static uint32_t plugin2(void *p){(void)p;return plugin_action(2u);}
static uint32_t plugin3(void *p){(void)p;return plugin_action(3u);}
static const uintptr_t native_plugin_callbacks[4]={(uintptr_t)plugin0,(uintptr_t)plugin1,(uintptr_t)plugin2,(uintptr_t)plugin3};
#ifdef FX_C110_FAULT_TEST
static uint32_t write_text(void *context) {
    (void)context;State *s=state();
    if(s) s->last_action=(uint32_t)fx_c110_demo_request(s->demo,fx_card_test_menu.command);
    return s?1u:0u;
}
#endif
FxPtp *fx_c110_menu_ptp(void) {
    State *s=state();return s?&s->ptp:0;
}
FxC110Usb *fx_c110_menu_usb(void) {
    State *s=state();return s?&s->usb:0;
}
FxC110Rtos *fx_c110_menu_rtos(void) {
    State *s=state();return s?&s->rtos:0;
}
FxC110Observation *fx_c110_menu_observation(void) {State *s=state();return s?&s->observation:0;}
#ifdef FX_C110_SENSOR_TRACE
FxSensorTrace *fx_c110_menu_sensor_trace(void) {
    State *s=state();return s?atomic_load_explicit(&s->sensor_trace,memory_order_acquire):0;
}
FxSensorTrace *fx_c110_menu_sensor_trace_create(void) {
    State *s=state();if(!s)return 0;
    FxSensorTrace *trace=atomic_load_explicit(&s->sensor_trace,memory_order_acquire);
    if(trace)return trace;
    uint32_t zero=0;
    if(!atomic_compare_exchange_strong_explicit(&s->sensor_trace_created,&zero,1u,memory_order_acquire,memory_order_relaxed))return 0;
    trace=FN(UINT64_C(0x40f41084),FxSensorTrace *(*)(uint32_t,uint32_t))((uint32_t)sizeof(*trace),0u);
    if(!trace)return 0; /* Retain the failed allocation attempt until reboot. */
    fx_sensor_trace_init(trace);atomic_store_explicit(&s->sensor_trace,trace,memory_order_release);return trace;
}
const uint32_t fx_c110_sensor_trace_owner_metadata[]={0x4658544fu,1u,offsetof(State,sensor_trace),offsetof(State,sensor_trace_created)};
#endif
#ifdef FX_C110_EVENT_TRACE
FxEventTrace *fx_c110_menu_event_trace(void) {
    State *s=state();return s?atomic_load_explicit(&s->event_trace,memory_order_acquire):0;
}
FxEventTrace *fx_c110_menu_event_trace_create(void) {
    State *s=state();if(!s)return 0;FxEventTrace *trace=atomic_load_explicit(&s->event_trace,memory_order_acquire);
    if(trace)return trace;uint32_t zero=0;
    if(!atomic_compare_exchange_strong_explicit(&s->event_trace_created,&zero,1u,memory_order_acquire,memory_order_relaxed))return 0;
    trace=FN(UINT64_C(0x40f41084),FxEventTrace *(*)(uint32_t,uint32_t))((uint32_t)sizeof(*trace),0u);
    if(!trace)return 0;fx_event_trace_init(trace);atomic_store_explicit(&s->event_trace,trace,memory_order_release);return trace;
}
const uint32_t fx_c110_event_trace_owner_metadata[]={0x4658454fu,1u,offsetof(State,event_trace),offsetof(State,event_trace_created)};
#endif
uint64_t fx_c110_observation_activation(void) {State *s=state();return s?s->startup.journal.token:0;}
int fx_c110_menu_ptp_active(void) {
    State *s=state();
    if(!s || !s->demo || atomic_load(&s->startup.shutdown)) return 0;
    uint32_t phase=fx_c110_demo_phase(s->demo);
    return phase==2u || phase==3u || phase==4u;
}
static uint32_t usb_toggle(void *context) {
    (void)context;State *s=state();
    if(s && fx_c110_menu_ptp_active()) {
        int enable=!atomic_load_explicit(&s->ptp.enabled,memory_order_acquire);
        if(!enable){fx_c110_observation_cancel(FX_OBS_LOCAL_REVOKE);fx_c110_usb_revoke();}
        fx_ptp_enable(&s->ptp,enable);
        (void)fx_c110_display_refresh_text(&s->page,0x7ff3u,USB_ID);
    }
    return s?1u:0u;
}

static int attach(void) {
    if (state()) return 1;
    Page *p=stock_page(2u,SETUP_ID);
    if (!p || p->descriptor!=UINT64_C(0xa028e70d0)) return 0;
    const Descriptor *d=(const Descriptor *)(uintptr_t)p->descriptor;
    if (d->count!=53u || d->rows!=UINT64_C(0x4403fbf0) || d->events!=UINT64_C(0x44040510) ||
        d->header!=UINT64_C(0x4403fae0)) return 0;
    const Row *rows=(const Row *)(uintptr_t)d->rows;
    const uint64_t *events=(const uint64_t *)(uintptr_t)d->events;
    if (*(volatile uint32_t *)(uintptr_t)UINT64_C(0xa030faed8)>=STOCK_LANGUAGE_COUNT) return 0;
    const uint8_t *font_map=*(const uint8_t *volatile *)(uintptr_t)UINT64_C(0xa030fafa8);
    const uintptr_t *attribute_roots=*(const uintptr_t *volatile *)(uintptr_t)UINT64_C(0xa030fafb8);
    if (!font_map || !attribute_roots) return 0;
    /* C1.10 has 30 locale roots, not 31. Extend every valid locale before
     * publishing custom IDs: a language change must not index a stock-sized
     * attribute table with an extension ID. Original text stays unchanged. */
    for(uint32_t language=0;language<STOCK_LANGUAGE_COUNT;++language)
        if(!attribute_roots[language]) return 0;
    if (rows[50].word[0]!=901u || rows[0].word[0]!=645u) return 0;
    /* C1.10 compact navigation table, without unrelated Setup property events. */
    const Event *source=(const Event *)(uintptr_t)UINT64_C(0x43f77ce8);
    uint32_t n;
    for(n=0;n<18u && (source[n].event&65535u)!=0x3fdu;++n) {}
    if (n==18u) return 0;
    State *s=FN(UINT64_C(0x40f41084),State *(*)(uint32_t,uint32_t))((uint32_t)sizeof(State),0u);
    if (!s) return 0;
    memset(s,0,sizeof(*s));
    fx_c110_ptp_init(&s->ptp);
    fx_c110_observation_init(&s->observation);
    fx_c110_startup_init(&s->startup);s->startup.context=s;
#ifdef FX_PLATFORM_PROFILE
    s->startup.files.observer=fx_capacity_file_observer(&s->capacity_registry);
#endif
    s->startup.activate=start_core;s->startup.core_phase=core_phase;
    s->startup.stop=stop_core;s->startup.refresh=refresh_startup;
    memcpy(&s->setup,d,sizeof(*d));
    memcpy(s->setup_rows+2,rows,53u*sizeof(Row));
    memcpy(s->setup_events+2,events,53u*sizeof(uint64_t));
    const uint32_t select_events[4]={0x2b7u,0x2bcu,0x2d0u,0x2dfu};
    for(uint32_t r=0;r<2u;++r) {
        s->setup_rows[r]=rows[50];
        s->setup_rows[r].word[0]=r?WONDER_LABEL_ID:LABEL_ID;
        s->setup_rows[r].word[2]=65535u;
        s->setup_rows[r].word[6]=r?0x7ff7u:0x7ff0u;
        if(fx_native_menu_link(s->entry_events[r],source,0x3fdu,select_events,
                              UINT64_C(0x403be318),r?WONDER_PAGE_ID:PAGE_ID)!=FX_OK)return 0;
        s->setup_events[r]=(uintptr_t)s->entry_events[r];
    }
    s->setup.rows=(uintptr_t)s->setup_rows;
    s->setup.events=(uintptr_t)s->setup_events;
    s->setup.count=55u;
    for(uint32_t i=0;i<5u;++i) s->callbacks[i]=(uintptr_t)noop;
    s->callbacks[0]=(uintptr_t)page_opened;
    s->callbacks[1]=(uintptr_t)page_closed;
    fx_native_page_initialize(&s->page,p,(PAGE_ID<<16)|2u,
                              (uintptr_t)&s->descriptor,s->callbacks,0);
    s->descriptor=*d;
    s->descriptor.count=ROW_COUNT;
    memcpy(s->header,(const void *)(uintptr_t)d->header,sizeof(s->header));
    /* Native title text element, independently correlated with English 644. */
    *(uint32_t *)(void *)(s->header+68u)=LABEL_ID;
    s->descriptor.header=(uintptr_t)s->header;
    s->descriptor.rows=(uintptr_t)s->rows;
    s->descriptor.events=(uintptr_t)s->events;
    const uint32_t ids[]={ACTIVATE_ID,USB_ID,AUTO_ID,GUARD_ID,
#ifdef FX_PLATFORM_PROFILE
        MANAGE_ID,
#endif
#ifdef FX_C110_FAULT_TEST
        FAULT_ID,
#endif
        BACK_ID
    };
    for(uint32_t r=0;r<ROW_COUNT;++r) {
        s->rows[r]=rows[50];
        s->rows[r].word[0]=ids[r];
        s->rows[r].word[2]=65535u;
        /* Original inline Setup rows omit bit6 (submenu chevron). Activate
         * is an action/status row; retain its independently bound events. */
        s->rows[r].word[4]&=~UINT32_C(0x40);
        if(ids[r]!=BACK_ID && ids[r]!=FAULT_ID)
            s->rows[r].word[4]|=1u; /* Original value child, independent of label. */
        s->rows[r].word[6]=ids[r]==ACTIVATE_ID?0x7ff1u:ids[r]==USB_ID?0x7ff3u:
            ids[r]==AUTO_ID?0x7ff8u:ids[r]==GUARD_ID?0x7ffau:ids[r]==FAULT_ID?0x7ff6u:0x7ff5u;
#ifdef FX_PLATFORM_PROFILE
        if(ids[r]==MANAGE_ID){s->rows[r].word[4]|=0x40u;s->rows[r].word[4]&=~1u;s->rows[r].word[6]=0x7ffcu;}
#endif
        memcpy(s->row_events[r],s->entry_events[0],(n+1u)*sizeof(Event));
        for(uint32_t i=0;i<=n;++i) {
            uint32_t e=(uint32_t)s->row_events[r][i].event&65535u;
            if(e==0x2b7u || e==0x2bcu || e==0x2d0u || e==0x2dfu) {
                s->row_events[r][i].callback=ids[r]==ACTIVATE_ID?(uintptr_t)activate:ids[r]==USB_ID?(uintptr_t)usb_toggle:
                    ids[r]==AUTO_ID?(uintptr_t)auto_toggle:
                    ids[r]==GUARD_ID?(uintptr_t)guard_clear:
#ifdef FX_PLATFORM_PROFILE
                    ids[r]==MANAGE_ID?(uintptr_t)manage_modules:
#endif
#ifdef FX_C110_FAULT_TEST
                    ids[r]==FAULT_ID?(uintptr_t)fault_test:
#endif
                    UINT64_C(0x403be318);
                s->row_events[r][i].next=ids[r]==BACK_ID?0x74au:0x749u;
#ifdef FX_PLATFORM_PROFILE
                if(ids[r]==MANAGE_ID)s->row_events[r][i].next=FX_NATIVE_MANAGER_PAGE_BASE;
#endif
            }
        }
        s->events[r]=(uintptr_t)s->row_events[r];
    }
    /* A full-width original text row keeps the abnormal-shutdown explanation
     * separate from the Activate label and its short Disabled value. Insert it
     * below Activate when opening a blocked page, before Nikon builds rows. */
    s->rows[ROW_COUNT]=s->rows[ROW_COUNT-1u];
    s->rows[ROW_COUNT].word[0]=VALUE_BASE+6u;
    s->rows[ROW_COUNT].word[6]=0x7ffbu;
    memcpy(s->row_events[ROW_COUNT],s->entry_events[0],(n+1u)*sizeof(Event));
    for(uint32_t i=0;i<=n;++i) {
        uint32_t e=(uint32_t)s->row_events[ROW_COUNT][i].event&65535u;
        if(e==0x2b7u || e==0x2bcu || e==0x2d0u || e==0x2dfu) {
            s->row_events[ROW_COUNT][i].callback=(uintptr_t)noop;
            s->row_events[ROW_COUNT][i].next=0x749u;
        }
    }
    s->events[ROW_COUNT]=(uintptr_t)s->row_events[ROW_COUNT];
    /* Separate native page and surface: plugin content never shares the core
     * page's selection, scroll or GUI object lifetime. */
    s->wonder_page=s->page;
    s->wonder_page.identity=(WONDER_PAGE_ID<<16)|2u;
    s->wonder_page.descriptor=(uintptr_t)&s->wonder_descriptor;
    s->wonder_descriptor=s->descriptor;
    s->wonder_descriptor.count=4u;
    memcpy(s->wonder_header,s->header,sizeof(s->wonder_header));
    *(uint32_t *)(void *)(s->wonder_header+68u)=WONDER_LABEL_ID;
    s->wonder_descriptor.header=(uintptr_t)s->wonder_header;
    s->wonder_descriptor.rows=(uintptr_t)s->wonder_rows;
    s->wonder_descriptor.events=(uintptr_t)s->wonder_events;
    const uint32_t wonder_ids[4]={DEMO_ID,MODULE_ID,WRITE_ID,BACK_ID};
    const uintptr_t wonder_callbacks[4]={
#ifdef FX_C110_FAULT_TEST
        (uintptr_t)demo,(uintptr_t)module_toggle,(uintptr_t)write_text,
#else
        (uintptr_t)noop,(uintptr_t)noop,(uintptr_t)noop,
#endif
        UINT64_C(0x403be318)};
    const uint32_t wonder_widgets[4]={0x7ff2u,0x7ff9u,0x7ff4u,0x7ff5u};
    for(uint32_t r=0;r<4u;++r) {
        s->wonder_rows[r]=rows[50];
        s->wonder_rows[r].word[0]=wonder_ids[r];
        s->wonder_rows[r].word[2]=65535u;
        s->wonder_rows[r].word[4]&=~UINT32_C(0x40);
        if(r==1u || r==2u)s->wonder_rows[r].word[4]|=1u;
        s->wonder_rows[r].word[6]=wonder_widgets[r];
        memcpy(s->wonder_row_events[r],s->entry_events[1],(n+1u)*sizeof(Event));
        for(uint32_t i=0;i<=n;++i) {
            uint32_t e=(uint32_t)s->wonder_row_events[r][i].event&65535u;
            if(e==0x2b7u || e==0x2bcu || e==0x2d0u || e==0x2dfu) {
                s->wonder_row_events[r][i].callback=wonder_callbacks[r];
                s->wonder_row_events[r][i].next=r==3u?0x74au:0x749u;
            }
        }
        s->wonder_events[r]=(uintptr_t)s->wonder_row_events[r];
    }
    s->wonder_back=s->wonder_rows[3];s->wonder_back_event=s->wonder_events[3];
    for(uint32_t r=0;r<4u;++r) {
        s->plugin_rows[r]=s->wonder_rows[2];s->plugin_rows[r].word[0]=PLUGIN_LABEL_BASE+r;
        s->plugin_rows[r].word[6]=0x7fc0u+r;
        memcpy(s->plugin_events[r],s->wonder_row_events[2],sizeof(s->plugin_events[r]));
        for(uint32_t i=0;i<=n;++i) {
            uint32_t e=(uint32_t)s->plugin_events[r][i].event&65535u;
            if(e==0x2b7u || e==0x2bcu || e==0x2d0u || e==0x2dfu)
                s->plugin_events[r][i].callback=native_plugin_callbacks[r];
        }
    }
#ifndef FX_C110_FAULT_TEST
    s->wonder_rows[0]=s->wonder_back;s->wonder_events[0]=s->wonder_back_event;
    s->wonder_descriptor.count=1u;
#endif
    fx_settings_init(&s->settings);
    FxNativeTextBank *bank=0;
    if(fx_native_text_bank_create((FxIndexMemory){0,map_allocate,map_release},font_map,attribute_roots,
        STOCK_TEXT_COUNT,STOCK_LANGUAGE_COUNT,STOCK_TEXT_COUNT+CUSTOM_TEXT_COUNT,font_map[901],
        ((const uint8_t *)attribute_roots[1])[645],&bank)!=FX_OK)return 0;
    atomic_store_explicit(&s->text_bank,bank,memory_order_seq_cst);
    if(fx_text_regions_add(&s->text_regions,&s->state_text_region,s,sizeof(*s))!=FX_OK)return 0;
    if(fx_text_regions_add(&s->text_regions,&s->image_text_region,__fx_core_code_begin,
        (uintptr_t)__fx_core_code_end-(uintptr_t)__fx_core_code_begin)!=FX_OK)return 0;
#ifdef FX_PLATFORM_PROFILE
    s->ui_dispatch.text_regions=&s->text_regions;
#endif
    s->magic=MAGIC;s->installed=1u;
    __asm__ volatile("dmb ish":::"memory");
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa030fafa8)=(uintptr_t)bank->fonts;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa030fafb8)=(uintptr_t)bank->attributes;
    __asm__ volatile("dmb ish":::"memory");
    uint64_t expected=UINT64_C(0xa028e70d0);
    if(!atomic_compare_exchange_strong_explicit((_Atomic uint64_t *)&p->descriptor,&expected,(uintptr_t)s,
        memory_order_release,memory_order_acquire)) {
        /* Another native event/menu task won publication. Retain this tiny
         * metadata allocation: a font root may already reference it. Only the
         * published State can own the startup/core workers. */
        return state()!=0;
    }
    return 1;
}

extern uint64_t fx_c110_navigate_original(uint32_t);
uint64_t fx_c110_menu_navigate(uint32_t page) {
    uint32_t end=WONDER_PAGE_ID+1u;
#ifdef FX_PLATFORM_PROFILE
    end=FX_NATIVE_EXTENSION_PAGE_LIMIT;
#endif
    if(page>=PAGE_ID && page<end)
        return FN(0x403b9c74,uint64_t (*)(uint32_t,uint32_t))(2u,page);
    return fx_c110_navigate_original(page);
}

Page *fx_c110_menu_page(uint32_t group,uint32_t id) {
    id&=65535u;
#ifdef FX_PLATFORM_PROFILE
    State *custom=state();
    if(group==2u && id>=PAGE_ID && id<FX_NATIVE_EXTENSION_PAGE_LIMIT && !custom)return 0;
    if(group==2u && custom && fx_native_apps_is_page(id))return (Page *)apps_page(custom,id);
    if(group==2u && id>=FX_NATIVE_MANAGER_PAGE_BASE && id<FX_NATIVE_MANAGER_PAGE_BASE+FX_NATIVE_UI_PAGES) {
        FxNativeUi *u=module_ui(UINT32_MAX,0);return u?(Page *)fx_native_ui_page(u,id):0;
    }
    if(group==2u && id>=UI_PAGE_BASE && id<FX_NATIVE_EXTENSION_PAGE_LIMIT) {
        FxNativeUi *u=custom?fx_ui_dispatch_find_page(&custom->ui_dispatch,id):0;
        return u?(Page *)fx_native_ui_page(u,id):0;
    }
#endif
    /* Lookup also runs during Nikon initialization. Only the completed
     * AppFirstStartComplete event may install metadata; a lookup cannot
     * allocate or publish Fenestra state while original boot is in progress. */
    if(group==2u && (id==PAGE_ID || id==WONDER_PAGE_ID)) {
        State *s=state();
        if(s) {
            ++s->page_requests;
            if(id==PAGE_ID) {
                int notice=atomic_load(&s->startup.phase)==FX_STARTUP_ABNORMAL;
                if(notice && !s->notice_inserted) {
                    Row row=s->rows[ROW_COUNT];uint64_t event=s->events[ROW_COUNT];
                    for(uint32_t i=ROW_COUNT;i>1u;--i) {s->rows[i]=s->rows[i-1u];s->events[i]=s->events[i-1u];}
                    s->rows[1]=row;s->events[1]=event;s->notice_inserted=1u;
                } else if(!notice && s->notice_inserted) {
                    Row row=s->rows[1];uint64_t event=s->events[1];
                    for(uint32_t i=1;i<ROW_COUNT;++i) {s->rows[i]=s->rows[i+1u];s->events[i]=s->events[i+1u];}
                    s->rows[ROW_COUNT]=row;s->events[ROW_COUNT]=event;s->notice_inserted=0u;
                }
                s->descriptor.count=(uint16_t)(ROW_COUNT+s->notice_inserted);
            }
            if(id==WONDER_PAGE_ID) {
                uint32_t count=
#ifdef FX_C110_FAULT_TEST
                    3u;
#else
                    0u;
#endif
                for(uint32_t i=0;i<4u;++i) {
                    char label[32];uint32_t value;int enabled;
                    int present=fx_c110_module_menu(s->demo,i,label,&value,&enabled)!=0;
#ifdef FX_PLATFORM_PROFILE
                    FxNativeUi *u=module_ui(i,1);FxNativePage *ui_page=u?fx_native_ui_page(u,u->page_base):0;
                    if(u && !fx_native_ui_root_visible(u)){present=0;ui_page=0;}
                    else if(ui_page && u->count)present=1;else ui_page=0;
#endif
                    if(!present)continue;
                    s->wonder_rows[count]=s->plugin_rows[i];
#ifdef FX_PLATFORM_PROFILE
                    if(ui_page)s->wonder_rows[count].word[4]=(s->wonder_rows[count].word[4]&~1u)|0x40u;
                    for(uint32_t j=0;j<18u;++j){uint32_t e=(uint32_t)s->plugin_events[i][j].event&65535u;
                        if(e==0x2b7u || e==0x2bcu || e==0x2d0u || e==0x2dfu)
                        {
                            s->plugin_events[i][j].next=ui_page?u->page_base:0x749u;
                            s->plugin_events[i][j].callback=ui_page?(uintptr_t)noop:native_plugin_callbacks[i];
                            s->plugin_events[i][j].condition=0u;
                        }
                        if(e==0x3fdu)break;
                    }
#endif
                    s->wonder_events[count]=(uintptr_t)s->plugin_events[i];++count;
                }
                s->wonder_rows[count]=s->wonder_back;s->wonder_events[count]=s->wonder_back_event;
                s->wonder_descriptor.count=(uint16_t)(count+1u);
            }
            return id==PAGE_ID?&s->page:&s->wonder_page;
        }
        return 0;
    }
#ifdef FX_PLATFORM_PROFILE
    if(group==2u && id>=PAGE_ID && id<FX_NATIVE_EXTENSION_PAGE_LIMIT)return 0;
#endif
    Page *p=stock_page(group,id);
#ifdef FX_C110_CAPTURE_EXPERIMENT
    if(group==2u && id==285u)fx_c110_capture_menu_attach(p);
#endif
    return p;
}

/* Stable action names. Status is a distinct original Nikon value object. */
uint32_t fx_c110_menu_row_state(uint32_t row_id,int *enabled) {
    State *s=state();*enabled=1;if(!s)return 0;
#ifdef FX_PLATFORM_PROFILE
    if(row_id>=0x7b00u && row_id<=0x7b14u)return fx_native_apps_value(s->apps,
        FN(UINT64_C(0x403e8e8c),FxNativePage *(*)(void))(),row_id,enabled);
#endif
#ifdef FX_PLATFORM_PROFILE
    if(row_id>=0x7d00u && row_id<0x7d40u) {
        FxNativeUi *u=module_ui(UINT32_MAX,0);return fx_native_ui_value(u,row_id,enabled);
    }
    if(row_id>=0x8000u && row_id<=65535u) {
        FxNativeUi *u=module_ui((row_id-0x8000u)/64u,0);return fx_native_ui_value(u,row_id,enabled);
    }
#endif
    uint32_t boot=atomic_load(&s->startup.phase),phase=fx_c110_demo_phase(s->demo);
    int active=s->demo && phase>=2u && phase<=4u && !atomic_load(&s->startup.shutdown);
    if(row_id>=0x7fc0u && row_id<0x7fc4u) {
#ifdef FX_PLATFORM_PROFILE
        FxNativeUi *u=module_ui(row_id-0x7fc0u,0);
        if(u && u->count) {
            *enabled=!atomic_load(&s->startup.shutdown) && active &&
                (u->owner->phase==FX_OWNER_RUNNING || u->owner->phase==FX_OWNER_FINISHED);
            return 0u;
        }
#endif
        char label[32];uint32_t value;
        if(!fx_c110_module_menu(s->demo,row_id-0x7fc0u,label,&value,enabled))*enabled=0;
        return PLUGIN_VALUE_BASE+row_id-0x7fc0u;
    }
    switch(row_id) {
    case 0x7ff1u:*enabled=!atomic_load(&s->startup.shutdown) &&
        (boot==FX_STARTUP_READY || boot==FX_STARTUP_IO_ERROR || (boot==FX_STARTUP_ARMED && phase==9u));return VALUE_BASE;
    case 0x7ff2u:case 0x7ff6u:*enabled=active && fx_c110_module_loaded(s->demo,"demo");return 0;
    case 0x7ff3u:*enabled=active;return VALUE_BASE+1u;
    case 0x7ff8u:return VALUE_BASE+2u;
    case 0x7ff9u:return VALUE_BASE+3u;
    case 0x7ffau:*enabled=boot==FX_STARTUP_ABNORMAL;return VALUE_BASE+4u;
    case 0x7ff4u:*enabled=active;return VALUE_BASE+5u;
    case 0x7ffbu:*enabled=0;return 0;
    default:return 0;
    }
}
static uint32_t page_opened(Page *p) {
    State *s=state();if(!s)return 1u;
    const Descriptor *descriptor=(const Descriptor *)p->descriptor;
    const Row *rows=(const Row *)descriptor->rows;uint32_t count=descriptor->count;
    for(uint32_t i=0;i<count;++i)
        (void)fx_c110_display_prepare_text(p,rows[i].word[6],rows[i].word[0]);
    return 1u;
}
static void fx_c110_menu_text_impl(int32_t language,int32_t id,const uint16_t **text,uint32_t *bytes) {
    State *s=state();
#ifdef FX_C110_CAPTURE_EXPERIMENT
    if(s && id>=(int32_t)FX_C110_CAPTURE_TEXT0 && id<(int32_t)(FX_C110_CAPTURE_TEXT0+FX_VIDEO_MAX_MODES)) {
        FxC110CaptureMenu *capture=fx_c110_capture_menu();uint32_t index=(uint32_t)id-FX_C110_CAPTURE_TEXT0;
        static const uint16_t empty[]=u"";*text=empty;*bytes=0;
        if(capture && atomic_load_explicit(&capture->ready,memory_order_acquire) && index<capture->hooks.configuration->count) {
            *text=capture->labels[index];uint32_t length=0;while(length<47u && (*text)[length])++length;*bytes=length*2u;
        }
        return;
    }
#endif
#ifdef FX_PLATFORM_PROFILE
    if(s && id>=(int32_t)MODULE_TEXT_BASE && module_text(s,(uint32_t)id,text,bytes))return;
    if(s && id>=(int32_t)UI_TEXT_BASE && id<(int32_t)(UI_TEXT_BASE+FX_NATIVE_UI_TEXTS)) {
        FxNativeUi *u=module_ui(UINT32_MAX,0);if(fx_native_ui_text(u,(uint32_t)id,text,bytes))return;
        static const uint16_t empty[]=u"";*text=empty;*bytes=0u;return;
    }
#endif
    if(s && id>=(int32_t)LABEL_ID && id<(int32_t)(STOCK_TEXT_COUNT+CUSTOM_TEXT_COUNT)) {
        const uint16_t *v=0;uint32_t n=0;
        if(id>=(int32_t)PLUGIN_LABEL_BASE && id<(int32_t)(PLUGIN_VALUE_BASE+4u)) {
            uint32_t index=((uint32_t)id-PLUGIN_LABEL_BASE)%4u,value=0;int enabled=0;char label[32]={0};
            (void)fx_c110_module_menu(s->demo,index,label,&value,&enabled);
#ifdef FX_PLATFORM_PROFILE
            FxNativeUi *u=module_ui(index,0);
            if(u && id<(int32_t)PLUGIN_VALUE_BASE && u->count) {
                *text=u->labels[0];uint32_t length=0;while(length<FX_UI_TEXT && (*text)[length])++length;
                *bytes=length*2u;return;
            }
#endif
            if(id<(int32_t)PLUGIN_VALUE_BASE) {
                for(uint32_t i=0;i<32u;++i)s->plugin_labels[index][i]=(uint16_t)(uint8_t)label[i];
                while(n<31u && label[n])++n;*text=s->plugin_labels[index];*bytes=n*2u;
            } else {
                uint16_t reverse[10];uint32_t digits=0;
                do {reverse[digits++]=(uint16_t)('0'+value%10u);value/=10u;}while(value && digits<10u);
                for(uint32_t i=0;i<digits;++i)s->plugin_values[index][i]=reverse[digits-1u-i];
                s->plugin_values[index][digits]=0;*text=s->plugin_values[index];*bytes=digits*2u;
            }
            ++s->text_requests;return;
        }
#define CHOOSE(t) do {static const uint16_t value[]=u##t;v=value;n=(uint32_t)sizeof(value)-2u;} while(0)
        uint32_t boot=atomic_load(&s->startup.phase),phase=fx_c110_demo_phase(s->demo);
        uint32_t desired=atomic_load(&s->startup.desired);
#ifdef FX_C110_FAULT_TEST
        uint32_t current=atomic_load(&s->startup.activation);
#endif
        switch((uint32_t)id) {
        case LABEL_ID:CHOOSE("Fenestra");break;
        case WONDER_LABEL_ID:CHOOSE("WonderLantern");break;
        case ACTIVATE_ID:CHOOSE("Activate");break;
#ifdef FX_C110_FAULT_TEST
        case DEMO_ID:CHOOSE("demo");break;
        case WRITE_ID:CHOOSE("Write text");break;
        case MODULE_ID:CHOOSE("Module demo");break;
#endif
        case USB_ID:CHOOSE("USB Debug");break;
        case BACK_ID:CHOOSE("Back");break;
        case AUTO_ID:CHOOSE("Auto activate");break;
#ifdef FX_PLATFORM_PROFILE
        case MANAGE_ID:CHOOSE("Modules");break;
        case APPS_PREVIOUS_ID:CHOOSE("Previous");break;
        case APPS_NEXT_ID:CHOOSE("Next");break;
        case APPS_EMPTY_ID:CHOOSE("No loaded modules");break;
        case APPS_UNAVAILABLE_ID:CHOOSE("UI resources unavailable");break;
        case APPS_RETRY_ID:CHOOSE("Retry");break;
#endif
        case GUARD_ID:CHOOSE("Shutdown guard");break;
        case FAULT_ID:CHOOSE("Failsafe test");break;
        case VALUE_BASE:
            if(boot==FX_STARTUP_ABNORMAL){CHOOSE("Disabled");}
            else if(atomic_load(&s->startup.shutdown)){CHOOSE("Stopping");}
            else if(boot==FX_STARTUP_UNCHECKED){CHOOSE("Checking card");}
            else if(boot==FX_STARTUP_IO_ERROR || phase==9u){CHOOSE("Card error");}
            else if(boot==FX_STARTUP_FAULT || phase==5u){CHOOSE("Panic");}
            else if(s->demo && phase>=2u && phase<=4u){CHOOSE("Active");}
            else if(boot==FX_STARTUP_READY){CHOOSE("Ready");}
            else {CHOOSE("Loading");}break;
        case VALUE_BASE+1u:
            if(fx_c110_menu_ptp_active() && atomic_load(&s->ptp.enabled)){CHOOSE("On");}else{CHOOSE("Off");}break;
        case VALUE_BASE+2u:
            if(desired&FX_STARTUP_AUTO){CHOOSE("On");}else{CHOOSE("Off");}break;
#ifdef FX_C110_FAULT_TEST
        case VALUE_BASE+3u:
            if((boot==FX_STARTUP_RUNNING || boot==FX_STARTUP_ARMED) && ((desired^current)&FX_STARTUP_DEMO)) {
                if(desired&FX_STARTUP_DEMO){CHOOSE("Next On");}else{CHOOSE("Next Off");}
            }else if(desired&FX_STARTUP_DEMO){CHOOSE("On");}else{CHOOSE("Off");}break;
#endif
        case VALUE_BASE+4u:
            if(boot==FX_STARTUP_ABNORMAL){CHOOSE("Clear");}
            else if(boot==FX_STARTUP_RUNNING || boot==FX_STARTUP_ARMED){CHOOSE("Armed");}
            else if(boot==FX_STARTUP_READY){CHOOSE("Ready");}
            else{CHOOSE("Check card");}break;
#ifdef FX_C110_FAULT_TEST
        case VALUE_BASE+5u:
            if(fx_c110_demo_text_state(s->demo)==FX_TEXT_SAVED){CHOOSE("Saved");}
            else if(fx_c110_demo_text_state(s->demo)==FX_TEXT_FAILED){CHOOSE("Failed");}
            else{CHOOSE("");}break;
#endif
        case VALUE_BASE+6u:
            if(boot==FX_STARTUP_ABNORMAL){CHOOSE("Disabled due to abnormal shutdown");}else{CHOOSE("");}break;
        default:CHOOSE("");break;
        }
#undef CHOOSE
        *text=v;*bytes=n;++s->text_requests;return;
    }
    /* Exact original getter: initialized language roots, bounded native ID. */
    int32_t count=*(volatile int32_t *)(uintptr_t)UINT64_C(0xa028232bc);
    int32_t index=count>id?id:0;
    const uint32_t *offsets=((const uint32_t *const *)(uintptr_t)(*(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa030fafb0)))[language];
    const uint8_t *pool=((const uint8_t *const *)(uintptr_t)(*(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa030fafa0)))[language];
    *text=(const uint16_t *)(const void *)(pool+(int32_t)offsets[index]);
    *bytes=offsets[index+1]-offsets[index];
}
const uint32_t fx_native_font_index_metadata[]={0x46584649u,1u,
    offsetof(State,text_regions),offsetof(State,font_scope),sizeof(FxTextRegions),
    offsetof(FxTextRegions,root),offsetof(FxTextRegions,count),offsetof(FxTextRegion,begin),offsetof(FxTextRegion,end),
    offsetof(FxTextRegionBranch,child),offsetof(FxTextRegionBranch,prefix),offsetof(FxTextRegionBranch,bit),
    offsetof(FxNativeFont,error),sizeof(uintptr_t)*8u};
const FxNativeFontProfile fx_c110_font_profile={0x402f08f8u,0x402f090cu,UINT64_C(0xa0305bbe8),0x408d72b8u,0x408d67e8u,0x42ea7481u,170765u,0x408e2050u,0x408e17dcu};
void fx_c110_font_enter(const void *request) {
    State *s=state();int enabled=s && (atomic_load(&s->demo)!=0);
    fx_native_font_enter(enabled?&s->font_scope:0,&fx_c110_font_profile,enabled?&s->text_regions:0,request);
}
uint32_t fx_c110_font_line_metrics(uintptr_t engine,uintptr_t layout,int32_t dimensions[2]) {
    State *s=state();return fx_native_font_line_metrics(s?&s->font_scope:0,&fx_c110_font_profile,engine,layout,dimensions);
}
void fx_c110_font_leave(void) {
    State *s=state();fx_native_font_leave(s?&s->font_scope:0,&fx_c110_font_profile);
}
void fx_c110_menu_text(int32_t language,int32_t id,const uint16_t **text,uint32_t *bytes) {
    fx_c110_menu_text_impl(language,id,text,bytes);

}


const uint32_t fx_c110_menu_metadata[]={MAGIC,1u,sizeof(State),offsetof(State,magic),
    offsetof(State,settings),offsetof(State,page),offsetof(State,activation_requests),offsetof(State,demo)};
#ifdef FX_PLATFORM_PROFILE
const uint32_t fx_c110_menu_ui_metadata[]={0x46585549u,2u,offsetof(State,ui),sizeof(FxNativeUi),
    offsetof(FxNativeUi,owner),offsetof(FxNativeUi,revision),offsetof(FxNativeUi,presented_revision),
    offsetof(FxPlatformOwner,revision),offsetof(FxPlatformOwner,native_ui)};
const uint32_t fx_c110_manager_ui_metadata[]={0x46584d55u,1u,offsetof(State,management),
    offsetof(FxNativeModules,ui),offsetof(FxNativeModules,manager)+offsetof(FxModuleManager,ui)};
#endif
const uint32_t fx_c110_menu_layout[]={0x46584d32u,1u,SETUP_ID,PAGE_ID,WONDER_PAGE_ID,
    ROW_COUNT,4u,offsetof(State,wonder_page),offsetof(State,setup_rows),offsetof(State,rows),
    offsetof(State,wonder_rows),offsetof(State,last_action)};

void fx_c110_menu_system_event(uint32_t event,int before) {
    if(before) {
        State *s=state();
        if(s) {
#ifdef FX_PLATFORM_PROFILE
            if(s->management)fx_module_manager_shutdown(&s->management->manager);
#endif
            /* Mark every owned worker before shutdown revokes provider
             * admission. An in-flight status reply must not provoke a new
             * module Panic merely because the user powered off normally. */
            (void)fx_c110_demo_shutdown(s->demo);
#ifdef FX_C110_CAPTURE_EXPERIMENT
            fx_c110_capture_stop();
#endif
            fx_c110_startup_shutdown(&s->startup);
        }
        return;
    }
    if(event==20u) {
        /* Original AppFirstStartComplete handlers have already returned.
         * AllAppReadySyncHook and CardMountComplete are not substitutes:
         * their ordering depends on the original initialization schedule. */
        if(!attach())return;
    } else if(event!=25u)return;
    /* A mount notification can retry card inspection only after the ready
     * event installed state. It cannot bootstrap menus or create an early
     * owner when the card happens to mount before application readiness. */
    State *s=state();if(s) {
        (void)fx_c110_startup_start(&s->startup);
        (void)fx_c110_startup_request(&s->startup,FX_BOOT_INSPECT);
    }
}
const uint32_t fx_c110_startup_metadata[]={0x46584231u,1u,offsetof(State,startup),
    sizeof(FxC110Startup),offsetof(FxC110Startup,phase),offsetof(FxC110Startup,desired),
    offsetof(FxC110Startup,shutdown),offsetof(FxC110Startup,shutdown_done)};

const uint32_t fx_c110_ptp_owner_metadata[]={0x4658504fu,1u,offsetof(State,ptp),offsetof(State,usb)};
#ifdef FX_C110_CAPTURE_EXPERIMENT
const uint32_t fx_c110_capture_menu_metadata[]={0x4658434du,2u,offsetof(State,capture),
    offsetof(FxC110CaptureMenu,hooks)+offsetof(FxC110CaptureHooks,mode),
    offsetof(FxC110CaptureMenu,hooks)+offsetof(FxC110CaptureHooks,fault),
    offsetof(FxC110CaptureMenu,revision),offsetof(FxC110CaptureMenu,ready),
    offsetof(FxC110CaptureMenu,stopping),offsetof(FxC110CaptureMenu,stop_ack),
    offsetof(FxC110CaptureMenu,menu_attached),offsetof(FxC110CaptureMenu,last_result)};
#endif
