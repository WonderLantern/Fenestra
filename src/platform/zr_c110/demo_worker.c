/* C1.10 card demonstration. A normal native RTOS task owns storage,
 * Loader and animation. GUI callbacks only create/notify this bounded worker.
 * Modules run at EL0 in a separate owned view; original translation tables and
 * task frames are never edited. Display uses the original C1.10 GUI/render
 * services. An emulated card adapter exists only in explicit test builds.
 */
#include "fenestra_c110_demo.h"
#include "fenestra_demo.h"
#include "fenestra_c110_display.h"
#include "fenestra_c110_panic.h"
#include "fenestra_native_panic.h"
#include "fenestra_c110_observation.h"
#ifdef FX_C110_CAPTURE_EXPERIMENT
#include "fenestra_c110_capture_menu.h"
#endif
#include "fenestra_fault_test.h"
#include "fenestra_interrupt.h"
#include "fenestra_exfat.h"
#include "fenestra_nvme.h"
#include "fenestra_file_card.h"
#include "fenestra_module_load_buffer.h"
#include "fenestra_load_sequence.h"
#include "fenestra_shared_accounts.h"
#include "fenestra_memory.h"
#include "fenestra_simd.h"
#include "fenestra_ptp.h"
#include "fenestra_worker_wait.h"
#include "fenestra_worker_pace.h"
#include "fenestra_c110_usb.h"
#include "fenestra_startup.h"
#include "fenestra_resources.h"
#ifdef FX_NATIVE_SCENE
#include "fenestra_c110_scene.h"
#include "fenestra_native_scene.h"
#endif
#include "fenestra_c110_services.h"
#ifdef FX_PLATFORM_PROFILE
#include "fenestra_platform_native.h"
#include "fenestra_native_ui_callbacks.h"
#endif
#include <stdatomic.h>
#include "c110_demo_config.h"
#ifdef FX_NATIVE_FILE_TEST_BACKEND
#include "c110_file_card.h"
#endif

#define OWN_BYTES (1024u*1024u)
#define IMAGE_BYTES FX_NATIVE_MODULE_IMAGE_LIMIT
#define PACKAGE_BYTES FX_NATIVE_MODULE_PACKAGE_LIMIT
#define TABLES 16u
/* The linked core, rather than optional trace features, defines its RX span. */
extern const uint8_t __fx_c110_code_end[];
#define FN(a,t) ((t)(uintptr_t)(a))
#ifdef FX_C110_FAULT_TEST
static FxFileObserver load_file_observer(void) {
#ifdef FX_PLATFORM_PROFILE
    return fx_capacity_file_observer(fx_native_capacity_registry());
#else
    return (FxFileObserver){0};
#endif
}
#endif
#define PTE_RW FX_EXEC_RW_XN
#define PTE_RX ((PTE_RW|(UINT64_C(2)<<6))&~FX_EXEC_PXN)
enum { QUEUED,LOADING,ACTIVE,PLAYING,FINISHED,PANICKED,DISABLED,ARMED,BACKGROUND,CARD_WAIT };
_Static_assert(FX_INTEGRITY==10,"private exception return code");
typedef struct {
    uint32_t magic,version;
    _Atomic uint32_t phase,error,command;
    int32_t task_id;
    _Atomic uint32_t frame,invocations,locked,targets;
    uint64_t pool;
    uint32_t faults,reserved;
    uint64_t esr,elr,far;
    uint32_t context_checks,context_errors;
    uint64_t before[9],after[9];
} Public;
struct FxC110Demo {
    Public public;
    uintptr_t trap_anchor,user_anchor;
    uint64_t user_last_spsr,user_last_elr;
    uint32_t user_entries,user_exits;
    /* EL1-only continuation: x0..x30, SP, PC, PSTATE, pending, IRQ count. */
    uint64_t user_context[36];
    uint64_t simd_enabled,simd_reserved;
    _Alignas(16) FxSimdState module_simd;
    FxLease active_lease;
    uint64_t invocation_deadline;
    void *allocation;
    uintptr_t base,end;
    FxSettings *settings,*ui_settings;
    FxSettings owned_settings;
    FxExtension extension;
#ifdef FX_NATIVE_FILE_TEST_BACKEND
    FxNvme nvme;
    FxExfat volume;
#endif
    FxCardIO card;
    FxFileCard native_file;
    uint8_t *file_buffer;
#ifdef FX_C110_FAULT_TEST
    union { FxDemoServices services; FxFaultServices fault_services; };
    FxDemoStats stats;
    FxDemoScene scene[2];
    FxDemoDraw draw_commands[2][FX_DEMO_DRAW_CAPACITY];
    FxC110Display display;
#endif
    void *page,*text_page;
    FxExecArena arena;
    uint8_t *package,*image,*stack;
    uint64_t (*table)[512];
    uint32_t table_count,operation;
#ifdef FX_C110_FAULT_TEST
    uint32_t scene_buffer;
#endif
    uint64_t (*user_table)[512];
    uint32_t user_table_count,user_returns;
    uint8_t *user_stack;
    FxFaultServices *user_services;
#ifdef FX_C110_FAULT_TEST
    FxDemoStats *user_stats;
    FxDemoScene *user_scene;
    FxDemoDraw *user_commands;
#endif
    volatile uint64_t *image_leaf;
    FxTextReport text;
    _Atomic uint32_t text_state;
    uint32_t text_refresh;
    struct {
        uint32_t entries;
        int32_t module_result;
        _Atomic uint32_t rejected_commands,idle_ticks;
        uint32_t ui_refresh,release_allowed;
        uint32_t software_interrupts,interrupt_reason,page_result,page_shown;
        uint32_t return_valid;
    } panic;
#ifdef FX_C110_FAULT_TEST
    FxC110Display panic_display;
#else
    FxNativePanicPage *panic_page;
#endif
    uint32_t background_armed,shooting_ticks,shooting_entries,shooting_id;
    uint64_t shooting_view;
    uint32_t card_load_attempts;
    uint32_t module_selection;
    _Atomic uint32_t stopping,stopped;
    uint32_t generic,owner_id;
    _Atomic uint32_t catalog_loaded;
    FxDiscoveredModule entry;
    FxServiceOwner service;
    FxC110Services native;
    uint8_t service_memory[4096];
    FxServiceExchange *exchange;
    _Atomic(FxC110Demo *) *children;
    uint32_t children_capacity;
    _Atomic uint32_t child_count;
    uint32_t last_view;
    uint64_t next_tick;
    FxExecLayout layout;
    FxC110Demo *fault_source;
    _Atomic uint32_t observer_fault;
    FxWorkerWait wait;uint64_t tick_due;FxWorkerPace pace;
    FxModuleLoadBuffer load_buffer;
    FxRuntimeAccounts accounting;

#ifdef FX_NATIVE_FILE_TEST_BACKEND
    FxC110FileTestCard file_test_card;
#endif
};
_Static_assert(sizeof(Public)==232,"Paused native diagnostic record");
_Static_assert(offsetof(FxC110Demo,trap_anchor)==232,"private exception frame anchor");
_Static_assert(offsetof(FxC110Demo,user_anchor)==240,"EL0 return frame anchor");
_Static_assert(offsetof(FxC110Demo,user_last_spsr)==248,"EL0 saved exception state");
_Static_assert(offsetof(FxC110Demo,user_entries)==264,"EL0 entry counter");
_Static_assert(offsetof(FxC110Demo,user_exits)==268,"EL0 exit counter");
_Static_assert(offsetof(FxC110Demo,user_context)==272,"EL0 IRQ continuation");
_Static_assert(offsetof(FxC110Demo,simd_enabled)==FX_MODULE_SIMD_ENABLE_OFFSET,"SIMD capability offset");
_Static_assert(offsetof(FxC110Demo,module_simd)==FX_MODULE_SIMD_OFFSET,"SIMD continuation offset");
_Static_assert(FX_INTERRUPTED==13,"private IRQ return code");
typedef struct {
    uint32_t attributes,reserved;
    uintptr_t context,entry;
    uint32_t priority,reserved2;
    uint64_t stack_bytes;
    uintptr_t stack;
    uint32_t processor,reserved3;
} NativeTask;
_Static_assert(sizeof(NativeTask)==56,"Independently inspected C1.10 task creation record");
extern FxResult fx_emulated_nvme(FxNvme *,FxBlockIO *);
extern int fx_c110_private_call(uintptr_t,int (*)(FxC110Demo *),FxC110Demo *);
extern int fx_c110_user_call(uintptr_t,const FxModuleServices *,uintptr_t,uintptr_t,FxC110Demo *);
extern const uint8_t fx_c110_user_return[];
static void registers(uint64_t out[9]) {
    __asm__ volatile("mrs %0,daif; mrs %1,ttbr0_el1; mrs %2,ttbr1_el1; mrs %3,tcr_el1;"
        "mrs %4,mair_el1; mrs %5,vbar_el1; mrs %6,tpidrro_el0; mrs %7,spsel; mrs %8,CurrentEL"
        :"=r"(out[0]),"=r"(out[1]),"=r"(out[2]),"=r"(out[3]),"=r"(out[4]),"=r"(out[5]),
         "=r"(out[6]),"=r"(out[7]),"=r"(out[8])::"memory");
}
static FxResult private_checked(FxC110Demo *d,int (*operation)(FxC110Demo *)) {
    registers(d->public.before);
    FxResult r=(FxResult)fx_c110_private_call((uintptr_t)d->table[0],operation,d);
    registers(d->public.after);
    ++d->public.context_checks;
    for(uint32_t i=0;i<9u;++i) {
        if(d->public.before[i]!=d->public.after[i]) {
            ++d->public.context_errors;
            return FX_INTEGRITY;
        }
    }
    return r;
}
static uint64_t now(void) {
    uint64_t n,f;
    __asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0":"=r"(n),"=r"(f));
    return f?n*1000u/f:0u;
}
static uintptr_t align(uintptr_t n,uintptr_t boundary) { return (n+boundary-1u)&~(boundary-1u); }
static int identity(uintptr_t va) {
    uint64_t par;
    __asm__ volatile("at s1e1r,%1; isb; mrs %0,par_el1":"=r"(par):"r"(va):"memory");
    return !(par&1u) && (par&FX_EXEC_PA_MASK)==(va&FX_EXEC_PA_MASK);
}
static volatile uint64_t *leaf_in(uint64_t (*tables)[512],uint32_t *count,uint32_t tables_capacity,uintptr_t va) {
    uint64_t *table=tables[0];
    for(uint32_t shift=30u;shift>12u;shift-=9u) {
        uint32_t slot=(uint32_t)((va>>shift)&511u);
        if(!table[slot]) {
            if(*count>=tables_capacity) return 0;
            table[slot]=(uintptr_t)tables[(*count)++]|3u;
        }
        if((table[slot]&3u)!=3u) return 0;
        table=(uint64_t *)(uintptr_t)(table[slot]&FX_EXEC_PA_MASK);
    }
    return &table[(va>>12)&511u];
}
static volatile uint64_t *leaf(FxC110Demo *d,uintptr_t va) {
    return leaf_in(d->table,&d->table_count,d->layout.tables,va);
}
static int map(FxC110Demo *d,uintptr_t start,uintptr_t end,uint64_t permission) {
    for(uintptr_t va=start;va<end;va+=4096u) {
        if(!identity(va)) return 0;
        volatile uint64_t *p=leaf(d,va);
        if(!p || (*p && *p!=(va|permission))) return 0;
        *p=va|permission;
    }
    return 1;
}
static int private_view(FxC110Demo *d) {
    d->table_count=1u;
    memset(d->table,0,d->layout.tables*4096u);
    if(!map(d,d->base,d->end,PTE_RW) || !map(d,0x401a0000u,
       align((uintptr_t)__fx_c110_code_end,4096u),
       PTE_RX)) return 0;
    d->image_leaf=leaf(d,(uintptr_t)d->image);
    return d->image_leaf!=0;
}
static int user_map(FxC110Demo *d,uintptr_t start,uintptr_t end,uint64_t flags) {
    for(uintptr_t va=start;va<end;va+=4096u) {
        if(!identity(va)) return 0;
        volatile uint64_t *p=leaf_in(d->user_table,&d->user_table_count,d->layout.tables,va);
        if(!p) return 0;
        *p=va|flags;
    }
    return 1;
}
static int user_view(FxC110Demo *d) {
    d->user_table_count=1u;
    memset(d->user_table,0,d->layout.tables*4096u);
    /* Published image holes are deliberately unmapped even at EL1. Do not
     * identity-probe those holes while creating the otherwise private view. */
    if(!user_map(d,d->base,(uintptr_t)d->image,PTE_RW) ||
       !user_map(d,(uintptr_t)d->image+d->layout.image_bytes,d->end,PTE_RW) ||
       !user_map(d,0x401a0000u,
       align((uintptr_t)__fx_c110_code_end,4096u),
       PTE_RX)) return 0;
    for(uint32_t i=0u;i<d->arena.pages;++i) {
        volatile uint64_t *p=leaf_in(d->user_table,&d->user_table_count,d->layout.tables,(uintptr_t)d->image+i*4096u);
        if(!p) return 0;
        uint64_t value=d->arena.expected[i];
        /* EL0 access, same per-segment write permission. EL1 never executes
         * module text, and EL0 never executes data or trusted Loader code. */
        if(value) {
            uint32_t executable=!(value&FX_EXEC_PXN);
            value|=(UINT64_C(1)<<6)|FX_EXEC_PXN|FX_EXEC_UXN;
            if(executable) value&=~FX_EXEC_UXN;
        }
        *p=value;
    }
    const uint64_t rw=PTE_RW|(UINT64_C(1)<<6),ro=rw|(UINT64_C(1)<<7);
    uintptr_t trampoline=(uintptr_t)fx_c110_user_return;
    if(trampoline&4095u) return 0;
    if(!user_map(d,(uintptr_t)d->user_services,(uintptr_t)d->user_services+4096u,ro) ||
       !user_map(d,(uintptr_t)d->exchange,(uintptr_t)d->exchange+4096u,rw) ||
       !user_map(d,(uintptr_t)d->user_stack,(uintptr_t)d->user_stack+d->layout.user_stack_bytes,rw) ||
       !user_map(d,trampoline,trampoline+4096u,(ro|FX_EXEC_PXN)&~FX_EXEC_UXN))return 0;
#ifdef FX_C110_FAULT_TEST
    if(!d->generic)return user_map(d,(uintptr_t)d->user_scene,(uintptr_t)d->user_scene+4096u,rw) &&
        user_map(d,(uintptr_t)d->user_commands,align((uintptr_t)d->user_commands+sizeof(d->draw_commands[0]),4096u),rw);
#endif
    return 1;
}
static FxResult start_module(FxSettings *,FxC110Demo **,void *,void *,uint32_t,const FxDiscoveredModule *,uint32_t);
static FxResult user_invoke(FxC110Demo *d,int *returned) {
    FxLoader *loader=&d->extension.loader;
    FxResult r=fx_exec_live(&d->arena);
    if(r!=FX_OK || !d->arena.published) return r==FX_OK?FX_STATE:r;
    if(!d->user_context[34]) {
    uint64_t clock=now();
    r=fx_execution_resume(&loader->runtime,clock);
    if(r==FX_OK)r=fx_acquire(&loader->runtime,loader->runtime.generation,clock,&d->active_lease);
    if(r!=FX_OK) return r;
    d->invocation_deadline=now()+(d->generic?d->entry.budget_ms:100u);
    if(d->generic) {
        FxServiceModule *api=(FxServiceModule *)d->user_services;
        *api=(FxServiceModule){{FX_ABI,sizeof(*api),0,0},FX_SERVICE_ABI,d->owner_id,
            d->service.generation,d->service.capabilities,now(),d->exchange,FX_EVENT_TICK,0};
    } else {
#ifdef FX_C110_FAULT_TEST
    *d->user_services=d->fault_services;
    d->user_services->demo.base.context=0;
    d->user_services->demo.base.log=0;
    d->user_services->demo.canvas=0;
    d->user_services->demo.stats=d->user_stats;
    d->user_services->demo.scene=d->user_scene;
    *d->user_scene=(FxDemoScene){1280u,800u,FX_DEMO_DRAW_CAPACITY,0u,d->user_commands,0,0u,0u};
#else
        return FX_CAPABILITY;
#endif
    }
    }
    *returned=fx_c110_user_call((uintptr_t)loader->image+loader->module.entry_offset,
        &d->user_services->demo.base,(uintptr_t)d->user_table[0],(uintptr_t)d->user_stack+d->layout.user_stack_bytes,d);
    ++d->user_returns;
    d->user_context[34]=0;
    uint64_t clock=now();
    r=fx_release(&loader->runtime,d->active_lease,clock);
    if(r==FX_OK)r=fx_execution_idle(&loader->runtime,clock);
    if(r!=FX_OK) return r;
    if(*returned || d->generic) return FX_OK; /* No scene promised by generic ABI. */
#ifdef FX_C110_FAULT_TEST
    /* Never hand an EL0-controlled descriptor or pointer to the native GUI.
     * Copy only from our fixed exchange region into a trusted scene buffer. */
    const FxDemoScene *s=d->user_scene;
    if(s->width!=1280u || s->height!=800u || s->capacity!=FX_DEMO_DRAW_CAPACITY ||
       s->commands!=d->user_commands || !s->count || s->count>FX_DEMO_DRAW_CAPACITY) return FX_INTEGRITY;
    FxDemoScene *trusted=&d->scene[d->scene_buffer];
    trusted->count=s->count;
    memcpy(trusted->commands,d->user_commands,s->count*sizeof(FxDemoDraw));
    d->stats=*d->user_stats;
    return r;
#else
    return FX_CAPABILITY;
#endif
}
void fx_c110_private_fault(FxC110Demo *d,uint64_t esr,uint64_t elr,uint64_t far,uint64_t argument) {
    if(!d->public.faults) { d->public.esr=esr;d->public.elr=elr;d->public.far=far; }
    ++d->public.faults;
    if(esr>>26==0x15u && (esr&65535u)==FX_INTERRUPT_PANIC_SVC) {
        ++d->panic.software_interrupts;
        if(d->panic.software_interrupts==1u) d->panic.interrupt_reason=(uint32_t)argument;
    }
}
static int private_operation(FxC110Demo *d) {
    FxResult r;
    if(d->operation==0u) {
        d->public.reserved=6u;
        r=fx_exec_bind(&d->arena,(uintptr_t)d->image,(uintptr_t)d->image,d->layout.image_bytes,d->image_leaf,
            (FxExecStorage){(uint64_t *)(d->base+d->layout.expected),(uint64_t *)(d->base+d->layout.staging),d->layout.image_bytes/4096u});
        d->public.reserved=7u;
        if(r==FX_OK) r=fx_settings_publish_mapped(d->settings,&d->arena,now());
        if(r==FX_OK && !user_view(d)) r=FX_CAPABILITY;
        if(r==FX_OK)r=fx_execution_idle(&d->extension.loader.runtime,now());
        d->public.reserved=8u;
        return (int)r;
    }
    if(d->operation==2u) return (int)fx_settings_confirm_stopped(d->settings,now());
    /* No native settings pointer or RTOS call is used while in this view. */
    if(d->user_context[34] && now()>=d->invocation_deadline)return FX_TIMEOUT;
    int returned=-1;
    r=fx_heartbeat(&d->extension.loader.runtime,now());
    if(r==FX_OK) {
        if(!d->user_context[34])++d->panic.entries;
        d->panic.module_result=INT32_MIN; /* Remains unset if the CPU traps. */
        d->panic.return_valid=0u;
        r=user_invoke(d,&returned);
        if(r==FX_OK){d->panic.module_result=returned;d->panic.return_valid=1u;}
    }
    if(r==FX_OK && returned!=0) r=FX_INTEGRITY;
    return (int)r;
}
static void display_restore(FxC110Demo *d) {
#ifdef FX_C110_FAULT_TEST
    fx_c110_display_detach(&d->display);
#else
    (void)d;
#endif
}
static FxC110Display *panic_display(FxC110Demo *d) {
#ifdef FX_C110_FAULT_TEST
    return &d->panic_display;
#else
    return d->panic_page?&d->panic_page->display:0;
#endif
}
static void panic_detach(FxC110Demo *d) {
    FxC110Display *display=panic_display(d);if(display)fx_c110_display_detach(display);
}
static void fail(FxC110Demo *d,FxResult r) {
    fx_c110_observation_cancel(FX_OBS_LOCAL_REVOKE);
#ifdef FX_C110_CAPTURE_EXPERIMENT
    fx_c110_capture_stop();
#endif
    fx_c110_usb_revoke();
    if(d->generic)fx_service_fault(&d->service);
    if(atomic_load_explicit(&d->public.phase,memory_order_acquire)==PANICKED) return;
    if(r==FX_OK) r=FX_INTEGRITY;
    if(d->settings->extension) {
        FxRuntime *runtime=&d->extension.loader.runtime;
        if(runtime->phase!=FX_PANIC) (void)fx_panic(runtime,r,d->public.esr);
        r=runtime->first_fault.reason;
        d->settings->stage=FX_PAGE_PANIC;
        d->settings->last_result=r;
        fx_debug_enable(&d->extension.debug,0);
    }
    atomic_store_explicit(&d->public.error,(uint32_t)r,memory_order_relaxed);
    atomic_store_explicit(&d->public.phase,PANICKED,memory_order_release);
    atomic_store_explicit(&d->public.command,0u,memory_order_release);
    for(uint32_t i=0;i<atomic_load(&d->child_count);++i) {
        FxC110Demo *child=atomic_load_explicit(&d->children[i],memory_order_acquire);
        if(child){atomic_store_explicit(&child->stopping,2u,memory_order_release);fx_worker_wait_signal(&child->wait);}
    }
    display_restore(d);
    d->panic.release_allowed=(uint32_t)fx_loader_release_allowed(&d->extension.loader);
    *d->ui_settings=d->owned_settings;
    d->panic.ui_refresh=(uint32_t)fx_c110_display_refresh_text(d->page,0x7ff1u,3480u);
    if(d->generic)return; /* Parent owns the single camera error surface. */
    FxC110Demo *source=d->fault_source?d->fault_source:d;
    FxC110View view=fx_c110_current_view();
    /* Presentation follows the actual native application, independently of
     * whether this error came from an armed diagnostic or a normal module. */
    uint32_t shooting=view.id==3u && view.state==1u;
    FxC110PanicReport report={.module=source->generic?source->entry.file:FX_C110_MODULE_PATH,
        .error=r,.module_result=source->panic.module_result,.cpu_faults=source->public.faults,
        .software_interrupts=source->panic.software_interrupts,.interrupt_reason=source->panic.interrupt_reason,
        .esr=source->public.esr,.elr=source->public.elr,.far=source->public.far,.shooting=shooting,
        .return_valid=source->panic.return_valid};
    if(source->generic)fx_panic_service_request(&report,&source->service);
#ifdef FX_C110_FAULT_TEST
    FxDemoScene *scene=&d->scene[d->scene_buffer];scene->count=0u;
    FxResult page=fx_native_panic_show(&d->panic_display,scene,&report);
#else
    if(!d->panic_page){d->panic.page_result=FX_BUDGET;return;}
    d->panic_page->report=report;d->panic_page->captured=1u;
    FxResult page=fx_native_panic_poll(d->panic_page);
#endif
    d->panic.page_result=(uint32_t)page;
    d->panic.page_shown=page==FX_OK?1u:0u;
}
static void *load_allocate(void *context,uint32_t bytes) {
    /* C1.10 independently initializes Heap at index 1; package scratch never
     * becomes executable. The original release wrapper selects its pool. */
    (void)context;return FN(UINT64_C(0x40f41084),void *(*)(uint32_t,uint32_t))(bytes,1u);
}
static void *account_allocate(void *context,uint32_t bytes) {
    /* Account rows outlive loading; keep them out of the temporary pool. */
    (void)context;return FN(UINT64_C(0x40f41084),void *(*)(uint32_t,uint32_t))(bytes,0u);
}
static FxAccountRow *controller_account(const char *name) {
#ifdef FX_PLATFORM_PROFILE
    return fx_shared_account_row(fx_native_shared_accounts(),name);
#else
    (void)name;return 0;
#endif
}
static void load_release(void *context,void *data) {
    (void)context;FN(UINT64_C(0x40f410c4),void (*)(void *))(data);
}
static FxResult load_prepare(FxC110Demo *d) {
    FxResult r;
    d->public.reserved=1u;
#ifdef FX_NATIVE_FILE_TEST_BACKEND
    /* Explicit QEMU fixture only. Production file loading never owns a
     * controller, registers a drive, or replaces the original device queues. */
    FxBlockIO block;
    r=fx_emulated_nvme(&d->nvme,&block);
    if(r!=FX_OK) return r;
    d->public.reserved=2u;
    r=fx_exfat_discover(&d->volume,&block);
    if(r!=FX_OK) return r;
    r=fx_c110_test_card_mount(&d->file_test_card,&d->nvme,&d->volume);
    if(r!=FX_OK) return r;
#endif
    if(d->generic) {
        uint32_t bytes=0;
        r=fx_c110_module_read(d->entry.file,d->file_buffer,d->layout.package_bytes+1u,&bytes);
        if(r!=FX_OK)return r;
        if(bytes!=d->entry.bytes)return FX_INTEGRITY;
        d->native_file=(FxFileCard){.buffer=d->file_buffer,.module_path=d->entry.file,.bytes=bytes,.valid=1u};
    } else {
#ifdef FX_C110_FAULT_TEST
    FxTextFiles files;
    FxC110FileScope file_scope;
    fx_c110_text_files(&files,&file_scope);
    r=fx_file_card_load_observed(&d->native_file,&files,FX_C110_NATIVE_MODULE_PATH,FX_C110_MODULE_PATH,
        d->file_buffer,d->layout.package_bytes+1u,FX_C110_DEMO_PACKAGE_SIZE,load_file_observer());
    if(r!=FX_OK) return r;
#else
        return FX_CAPABILITY;
#endif
    }
    fx_file_card_io(&d->native_file,&d->card);
    FxActivation a={&d->card,d->generic?d->entry.file:FX_C110_MODULE_PATH,
        d->generic?d->entry.bytes:FX_C110_DEMO_PACKAGE_SIZE,
        d->generic?d->entry.digest:fx_c110_demo_package_sha,fx_c110_demo_firmware_sha,d->package,d->image,
        d->layout.package_bytes,d->layout.image_bytes,d->generic?d->service.capabilities:0u,(uint64_t)d->layout.bytes+d->load_buffer.bytes,now(),10000u};
    d->public.reserved=3u;
    r=fx_settings_load(d->settings,&d->extension,&a);
    if(r!=FX_OK) return r;
    if(d->generic && d->extension.loader.module.entry_abi!=FX_MODULE_ENTRY_SERVICES)return FX_CAPABILITY;
    if(d->generic && (d->extension.loader.module.stacks.native_bytes!=d->layout.native_stack_bytes ||
                     d->extension.loader.module.stacks.user_bytes!=d->layout.user_stack_bytes))return FX_INTEGRITY;
    return FX_OK;
}
static FxResult load(FxC110Demo *d) {
    FxResult r=fx_module_load_buffer_open(&d->load_buffer,
        (FxIndexMemory){0,load_allocate,load_release},d->layout.package_bytes);
    if(r==FX_OK) {
        d->package=d->file_buffer=d->load_buffer.data;
        r=load_prepare(d);
    }
    FxResult cleanup=fx_module_load_buffer_finish(&d->load_buffer,&d->native_file,&d->extension.loader.card,r);
    d->package=d->file_buffer=0;
    if(r==FX_OK)r=cleanup;
    if(r==FX_OK)r=fx_loader_source_released(&d->extension.loader);
    if(r!=FX_OK)return r;
    d->public.reserved=4u;
    if(!private_view(d)) return FX_CAPABILITY;
    d->operation=0u;
    d->public.reserved=5u;
    r=private_checked(d,private_operation);
    d->public.reserved=9u;
    return r;
}
static void try_load(FxC110Demo *d) {
    atomic_store_explicit(&d->public.phase,LOADING,memory_order_release);
    ++d->card_load_attempts;
    FxResult r=load(d);
    *d->ui_settings=d->owned_settings;
    if(r==FX_IO && d->public.reserved==1u && !d->native_file.read_calls &&
       !d->native_file.close_error && fx_c110_card_not_ready(d->native_file.native_error)) {
        /* No descriptor acquired and no module published: let Nikon finish
         * discovering its card. One explicit retry, no mount/controller takeover. */
        atomic_store_explicit(&d->public.phase,CARD_WAIT,memory_order_release);
        (void)fx_c110_display_refresh_text(d->page,0x7ff1u,3480u);
    } else if(r!=FX_OK || d->public.faults) { fail(d,r); }
    else {
        atomic_store_explicit(&d->public.phase,ACTIVE,memory_order_release);
        /* Nikon caches row text; publish the new phase before invalidating it. */
        (void)fx_c110_display_refresh_text(d->page,0x7ff1u,3480u);
    }
}
#ifndef FX_C110_FAULT_TEST
typedef struct {FxC110Demo *parent,*child;} LoadWait;
static int load_cancelled(void *context){return atomic_load(&((LoadWait *)context)->parent->stopping)!=0u;}
static FxResult load_yield(void *context){(void)context;return FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(1u)?FX_IO:FX_OK;}
static FxResult load_poll(void *context) {
    LoadWait *wait=context;uint32_t phase=fx_c110_demo_phase(wait->child);
    if(phase==QUEUED || phase==LOADING || phase==CARD_WAIT)return FX_BUSY;
    if(phase==PANICKED) {
        wait->parent->fault_source=wait->child;
        FxResult error=(FxResult)atomic_load(&wait->child->public.error);
        return error==FX_OK?FX_INTEGRITY:error;
    }
    return FX_OK;
}
static FxResult start_children(FxC110Demo *d) {
    FxModuleInventory catalog={0};FxResult r=fx_module_load_buffer_open(&d->load_buffer,
        (FxIndexMemory){0,load_allocate,load_release},d->layout.package_bytes);
    if(r==FX_OK)r=fx_c110_modules_read(&catalog,d->load_buffer.data,d->load_buffer.bytes,fx_c110_demo_firmware_sha);
    FxResult cleanup=fx_module_load_buffer_finish(&d->load_buffer,0,0,r);
    if(r==FX_OK)r=cleanup;
    if(r!=FX_OK)return r;
    if(catalog.count>FX_MODULE_OWNER_LIMIT){fx_modules_destroy(&catalog);return FX_BUDGET;}
    if(catalog.count) {
        uint32_t bytes=catalog.count*(uint32_t)sizeof(*d->children);
        d->children=FN(UINT64_C(0x40f41084),void *(*)(uint32_t,uint32_t))(bytes,0u);
        if(!d->children){fx_modules_destroy(&catalog);return FX_BUDGET;}
        FxAllocationObserver observer=fx_account_observer(controller_account("loader.storage"));
        fx_allocation_observe(observer,bytes,FX_ALLOC_REQUESTED);
        memset(d->children,0,bytes);d->children_capacity=catalog.count;
        fx_allocation_observe(observer,bytes,FX_ALLOC_PUBLISHED);
    }
    for(uint32_t i=0;i<catalog.count && !atomic_load(&d->stopping);++i) {
        FxDiscoveredModule *e=&catalog.entries[i];
        if(!e->enabled)continue;
        if(e->required&~FX_SDK_CAPS){fx_modules_destroy(&catalog);return FX_CAPABILITY;}
        FxC110Demo *child=0;
        r=start_module(0,&child,d->page,d->text_page,d->module_selection,e,i+1u);
        atomic_store_explicit(&d->children[i],child,memory_order_release);
        atomic_store_explicit(&d->child_count,i+1u,memory_order_release);
        if(r!=FX_OK){fx_modules_destroy(&catalog);return r;}
        LoadWait wait={d,child};
        r=fx_load_sequence_wait((FxLoadSequence){&wait,load_poll,load_yield,load_cancelled});
        if(r==FX_INTERRUPTED && atomic_load(&d->stopping))break;
        if(r!=FX_OK){fx_modules_destroy(&catalog);return r;}
    }
    fx_modules_destroy(&catalog);d->catalog_loaded=1u;return FX_OK;
}
#endif
int fx_c110_modules_ready(FxC110Demo *d) {
    if(!d || !d->catalog_loaded)return 0;
    uint32_t core_phase=atomic_load(&d->public.phase);
    if(core_phase!=ACTIVE && core_phase!=PLAYING && core_phase!=FINISHED && core_phase!=BACKGROUND)return 0;
    for(uint32_t i=0;i<atomic_load(&d->child_count);++i) {
        FxC110Demo *child=atomic_load(&d->children[i]);
        if(child) {
            uint32_t phase=atomic_load(&child->public.phase);
            if(phase==QUEUED || phase==LOADING || phase==CARD_WAIT)return 0;
        }
    }
    return 1;
}
uint32_t fx_c110_module_loaded(FxC110Demo *d,const char *id) {
    if(!d || !id)return 0u;
    if(!atomic_load(&d->catalog_loaded))return 0u;
    for(uint32_t i=0;i<atomic_load(&d->child_count);++i) {
        FxC110Demo *child=atomic_load(&d->children[i]);
        if(child && !memcmp(child->entry.id,id,16u)) {
            uint32_t phase=atomic_load(&child->public.phase);
            return phase!=QUEUED && phase!=LOADING && phase!=CARD_WAIT?1u:0u;
        }
    }
    return 0u;
}
static int scene_needs_poll(FxPlatformOwner *owner) {
#ifdef FX_NATIVE_SCENE
    return fx_native_scene_needs_poll(owner);
#else
    (void)owner;return 0;
#endif
}
static void park(FxC110Demo *d,uint64_t ticket,uint32_t ticks) {
    uint32_t expired=0u;FxResult r=fx_worker_wait_step(&d->wait,ticket,ticks,&expired);
    if(r!=FX_OK)(void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(ticks==UINT32_MAX?100u:ticks>1000u?1000u:ticks);
}
static void worker(void *argument) {
    FxC110Demo *d=argument;
    FxResult r=fx_settings_choose(d->settings,FX_UI_ACTIVATE,now(),1000u);
    if(r==FX_OK) r=fx_settings_take_activation(d->settings);
    if(r!=FX_OK) fail(d,r);
    else if(d->generic
#ifdef FX_C110_FAULT_TEST
        || (d->module_selection&FX_STARTUP_DEMO)
#endif
        )try_load(d);
    else {
        d->settings->stage=FX_PAGE_ACTIVE;*d->ui_settings=d->owned_settings;
        atomic_store_explicit(&d->public.phase,ACTIVE,memory_order_release);
    }
    for(;;) {
        uint64_t loop_ticket=fx_worker_wait_ticket(&d->wait);
        uint32_t observer_fault=atomic_exchange_explicit(&d->observer_fault,0u,memory_order_acquire);
        if(observer_fault)fail(d,(FxResult)observer_fault);
        if(d->generic) {
            fx_service_fault_poll(&d->service);
            FxPlatformOwner *owner=atomic_load_explicit(&d->native.platform,memory_order_acquire);
            if(owner && owner->phase==FX_OWNER_FAULT && fx_c110_demo_phase(d)!=PANICKED)
                fail(d,owner->first_error?(FxResult)owner->first_error:FX_INTEGRITY);
#ifdef FX_NATIVE_SCENE
            if(owner)fx_c110_scene_poll(owner);
#endif
            /* FINISH queued its terminal UI change. The retained dispatcher
             * retries BUSY; no further EL0 call or periodic UI resubmission. */
        }
#ifdef FX_C110_CAPTURE_EXPERIMENT
        if(!d->generic) {
            uint32_t capture_error=fx_c110_capture_poll();
            if(capture_error)fail(d,(FxResult)capture_error);
        }
#endif
        if(atomic_load_explicit(&d->stopping,memory_order_acquire)) {
            d->wait.armed=0u;
            if(d->generic && atomic_load_explicit(&d->stopped,memory_order_acquire)){park(d,loop_ticket,UINT32_MAX);continue;}
            fx_c110_usb_revoke();
            atomic_store(&d->public.command,0u);
            if(atomic_load_explicit(&d->stopping,memory_order_acquire)==2u) {
                /* Parent Panic: revoke dependent providers without resuming
                 * an interrupted module, restoring its patches, freeing its
                 * resources, or replacing the parent's first-fault surface. */
                if(d->generic)fx_service_fault(&d->service);
                display_restore(d);panic_detach(d);
                if(d->generic && d->service.stop_state!=FX_SERVICE_STOP_FAULT_PENDING)park(d,loop_ticket,UINT32_MAX);
                else (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(33u);
                continue;
            }
            /* Finish ONLY an existing interrupted invocation. Do not acquire
             * a new lease or publish its scene during shutdown. Otherwise an
             * ordinary power-off during a frame would leave a false flag. */
            if(d->user_context[34] && fx_c110_demo_phase(d)!=PANICKED) {
                d->operation=1u;
                r=private_checked(d,private_operation);
                if(r!=FX_OK && r!=FX_INTERRUPTED)fail(d,r);
            }
            display_restore(d);panic_detach(d);
            int children_stopped=1;
            for(uint32_t i=0;i<atomic_load(&d->child_count);++i)
                if(!fx_c110_demo_shutdown(atomic_load(&d->children[i])))children_stopped=0;
            int services_stopped=!d->generic || d->service.stop_state==FX_SERVICE_STOP_DONE;
            if(d->generic && !d->user_context[34] && !d->extension.loader.runtime.outstanding && !services_stopped) {
                r=fx_service_stop(&d->service);
                if(r!=FX_OK && r!=FX_BUSY)fail(d,r);
                services_stopped=r==FX_OK;
            }
            if(fx_c110_demo_phase(d)!=PANICKED && !d->extension.loader.runtime.outstanding && children_stopped && services_stopped)
                atomic_store_explicit(&d->stopped,1u,memory_order_release);
            (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(33u);
            continue;
        }
        if(!d->generic)fx_c110_ptp_poll(d);
        if(!d->generic && !d->catalog_loaded && (fx_c110_demo_phase(d)==ACTIVE || fx_c110_demo_phase(d)==FINISHED)) {
#ifdef FX_C110_FAULT_TEST
            d->catalog_loaded=1u; /* Old fault fixture is not a service-ABI module. */
#else
            r=start_children(d);if(r!=FX_OK)fail(d,r);
#endif
        }
        for(uint32_t i=0;i<atomic_load(&d->child_count);++i) {
            FxC110Demo *child=atomic_load_explicit(&d->children[i],memory_order_acquire);
            if(child && fx_c110_demo_phase(child)==PANICKED && fx_c110_demo_phase(d)!=PANICKED) {
                d->fault_source=child;fail(d,(FxResult)atomic_load(&child->public.error));
            }
            if(fx_c110_demo_phase(d)==PANICKED)(void)fx_c110_demo_shutdown(child);
        }
        if(d->generic) {
            uint32_t phase=fx_c110_demo_phase(d);
            uint32_t pacing=FX_WORKER_POLL;
            FxPlatformOwner *owner=atomic_load_explicit(&d->native.platform,memory_order_acquire);
            if(phase==ACTIVE && !d->user_context[34] && d->wait.armed) {
                uint64_t clock=now();uint32_t tick=owner?atomic_load_explicit(&owner->tick_subscriptions,memory_order_acquire):0u;
                if(tick && clock>=d->tick_due){fx_platform_pulse(owner,clock);d->tick_due=clock+d->entry.period_ms;}
                (void)fx_worker_wait_continue(&d->wait,tick || scene_needs_poll(owner)?10u:0u,&d->exchange->reply,&d->service);
                continue;
            }
            if(phase==ACTIVE && (d->user_context[34] || d->wait.resumed || now()>=d->next_tick)) {
                if(!d->user_context[34]) {
                    d->wait.resumed=0u;uint64_t clock=now();
                    if(clock>=d->tick_due){fx_platform_pulse(owner,clock);d->tick_due=clock+d->entry.period_ms;}
                }
                d->operation=1u;r=private_checked(d,private_operation);
                if(r!=FX_INTERRUPTED) {
                    if(r!=FX_OK || d->public.faults)fail(d,r);
                    else {
                        d->service.time_ms=now();
                        if(d->exchange->request.sequence) {
                            FxServiceReply reply;
                            uint64_t previous=d->service.last_sequence;
                            (void)fx_service_dispatch(&d->service,&d->exchange->request,&reply);
                            pacing=fx_worker_pace_reply(&d->pace,previous,d->exchange->request.sequence,&reply);
                            fx_ram_copy(&d->exchange->reply,&reply,sizeof(reply));
                            if(reply.result==FX_OK && d->exchange->request.operation==FX_S_LIFECYCLE &&
                               d->exchange->request.arg[0]==FX_LIFE_FINISH)
                                atomic_store_explicit(&d->public.phase,FINISHED,memory_order_release);
                            if(d->native.close_uncertain) {fx_service_fault(&d->service);fail(d,FX_CLOSE);}
                            else if(d->service.fault)fail(d,FX_INTEGRITY);
                        }
                        atomic_fetch_add(&d->public.invocations,1u);
                        uint32_t action=atomic_load(&d->native.menu_action);
                        if(action && fx_service_event(&d->service,FX_EVENT_ACTION,action)==FX_OK)
                            (void)atomic_compare_exchange_strong(&d->native.menu_action,&action,0u);
                        FxC110View view=fx_c110_current_view();
                        uint32_t v=(view.id<<16)|(view.state&65535u);
                        if(v!=d->last_view) {
                            if(fx_service_event(&d->service,FX_EVENT_VIEW,v)==FX_OK)d->last_view=v;
                        }
                        d->next_tick=now()+(pacing==FX_WORKER_POLL?d->entry.period_ms:0u);
                    }
                }
            }
            if(phase==ACTIVE && !d->user_context[34]) {
                r=fx_heartbeat(&d->extension.loader.runtime,now());if(r!=FX_OK)fail(d,r);
            }
            if(d->wait.armed)continue;
            if(pacing==FX_WORKER_CONTINUE && fx_c110_demo_phase(d)==ACTIVE)continue;
            if(d->user_context[34])(void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(1u);
            else if(fx_c110_demo_phase(d)!=ACTIVE) {
                if(d->service.stop_state==FX_SERVICE_STOP_FAULT_PENDING)(void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(10u);
                else park(d,loop_ticket,scene_needs_poll(owner)?10u:UINT32_MAX);
            } else (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(pacing==FX_WORKER_YIELD?1u:10u);
            continue;
        }
#ifdef FX_C110_FAULT_TEST
        /* Historical fault fixtures alone use the old scene-call ABI.
         * Production orchestrates ordinary copied-service workers only. */
        /* A paused invocation owns a live lease and partially written scene.
         * Defer UI actions until it returns; shutdown still retains the lease. */
        uint32_t command=d->user_context[34]?0u:
            atomic_exchange_explicit(&d->public.command,0u,memory_order_acquire);
        uint32_t phase=atomic_load_explicit(&d->public.phase,memory_order_relaxed);
        if(command==FX_C110_RETRY_CARD && phase==CARD_WAIT) {
            try_load(d);
            phase=atomic_load_explicit(&d->public.phase,memory_order_acquire);
        }
#ifdef FX_C110_FAULT_ON_SHOOTING
        if(command==FX_C110_TEST_FAULT && (phase==ACTIVE || phase==FINISHED)) {
            d->background_armed=1u;d->fault_services.armed=1u;d->services.frame=0u;
            atomic_store_explicit(&d->public.phase,ARMED,memory_order_release);
            phase=ARMED;
        }
#endif
        if(command==FX_C110_WRITE_TEXT) {
            if(phase==ACTIVE || phase==FINISHED) {
                FxTextFiles files;
                FxC110FileScope file_scope;
                atomic_store_explicit(&d->text_state,FX_TEXT_WRITING,memory_order_release);
#ifdef FX_NATIVE_FILE_TEST_BACKEND
                r=fx_c110_test_card_mount(&d->file_test_card,&d->nvme,&d->volume);
                if(r!=FX_OK) {
                    d->text=(FxTextReport){0x46585431u,1u,FX_TEXT_FAILED,(uint32_t)r,
                        d->file_test_card.error,0u,d->text.attempts+1u,0u};
                } else
#endif
                {
                fx_c110_text_files(&files,&file_scope);
                (void)fx_text_write_observed(&files,&d->text,load_file_observer());
                }
                atomic_store_explicit(&d->text_state,d->text.state,memory_order_release);
                d->text_refresh=(uint32_t)fx_c110_display_refresh_text(d->text_page,0x7ff4u,3483u);
            } else atomic_store_explicit(&d->text_state,FX_TEXT_FAILED,memory_order_release);
        }
        if((command==FX_C110_PLAY
#ifdef FX_C110_FAULT_TEST
            || command==FX_C110_TEST_FAULT
#endif
            ) && (phase==ACTIVE || phase==FINISHED)) {
            d->fault_services.armed=command==FX_C110_TEST_FAULT?1u:0u;
            d->services.frame=0u;
            /* Playback belongs to the module page; core status still uses d->page. */
            r=fx_c110_display_attach(&d->display,d->text_page,&d->scene[d->scene_buffer^1u]);
            if(r!=FX_OK) { fail(d,r);phase=PANICKED; }
            else {
                atomic_store_explicit(&d->public.phase,PLAYING,memory_order_release);
                phase=PLAYING;
            }
        }
        if(command==FX_C110_STOP && phase==PLAYING) {
            display_restore(d);
            atomic_store_explicit(&d->public.phase,FINISHED,memory_order_release);
            phase=FINISHED;
        }
        /* No hot teardown: activation lasts until camera restart. */
        if(phase==ARMED) {
            FxC110View view=fx_c110_current_view();
            d->shooting_id=view.id;
            if(view.id==3u && view.state==1u) {
                if(d->shooting_view==view.address) ++d->shooting_ticks;
                else { d->shooting_view=view.address;d->shooting_ticks=1u; }
                if(d->shooting_ticks>=30u) {
                    atomic_store_explicit(&d->public.phase,BACKGROUND,memory_order_release);
                    phase=BACKGROUND;
                }
            } else d->shooting_ticks=0u;
            r=fx_heartbeat(&d->extension.loader.runtime,now());
            if(r!=FX_OK) fail(d,r);
        }
        if(phase==PLAYING || phase==BACKGROUND) {
            if(phase==BACKGROUND) {
                FxC110View view=fx_c110_current_view();
                if(view.address!=d->shooting_view || view.id!=3u || view.state!=1u) {
                    atomic_store_explicit(&d->public.phase,ARMED,memory_order_release);
                    d->shooting_ticks=0u;
                    continue;
                }
                ++d->shooting_entries;
            }
            d->operation=1u;
            r=private_checked(d,private_operation);
            if(r==FX_INTERRUPTED) {
                if(now()>=d->invocation_deadline)fail(d,FX_TIMEOUT);
                /* Restoring native DAIF delivers the unacknowledged IRQ to
                 * Nikon. Its scheduler runs on its ORIGINAL tables/vectors. */
                (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(1u);
                continue;
            }
            if(r!=FX_OK || d->public.faults) fail(d,r);
            else if(d->stats.cell_count!=FX_DEMO_CELLS || d->stats.target_count!=314u ||
                    (d->public.pool && d->public.pool!=d->stats.cell_pool_address) ||
                    d->extension.loader.runtime.outstanding) fail(d,FX_INTEGRITY);
            else {
                d->public.pool=d->stats.cell_pool_address;
                r=phase==BACKGROUND?FX_OK:fx_c110_display_present(&d->display,&d->scene[d->scene_buffer]);
                if(r==FX_STATE && !d->display.attached) {
                    atomic_store_explicit(&d->public.phase,FINISHED,memory_order_release);
                    continue;
                }
                if(r!=FX_OK) { fail(d,r);continue; }
                d->scene_buffer^=1u;
                d->services.scene=&d->scene[d->scene_buffer];
                atomic_store(&d->public.frame,d->services.frame);
                atomic_store(&d->public.locked,d->stats.locked_count);
                atomic_store(&d->public.targets,d->stats.target_count);
                atomic_fetch_add(&d->public.invocations,1u);
                ++d->services.frame;
                if(d->services.frame>=FX_C110_DEMO_FRAME_LIMIT) {
                    display_restore(d);
                    atomic_store_explicit(&d->public.phase,FINISHED,memory_order_release);
                }
            }
        } else if((phase==ACTIVE || phase==FINISHED) && (d->module_selection&FX_STARTUP_DEMO)) {
            r=fx_heartbeat(&d->extension.loader.runtime,now());
            if(r!=FX_OK) fail(d,r);
        }
#endif
        if(atomic_load_explicit(&d->public.phase,memory_order_acquire)==PANICKED) {
            atomic_fetch_add_explicit(&d->panic.idle_ticks,1u,memory_order_relaxed);
#ifndef FX_C110_FAULT_TEST
            if(d->panic_page && d->panic_page->captured) {
                FxResult shown=fx_native_panic_poll(d->panic_page);
                d->panic.page_result=(uint32_t)shown;d->panic.page_shown=shown==FX_OK;
            }
#else
            FxC110Display *display=panic_display(d);
            if(display && display->attached && display->global_overlay &&
               fx_c110_current_view().address!=display->application)panic_detach(d);
#endif
        }
        /* Original task-delay primitive, never a busy loop in the GUI task. */
        (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(33u);
    }
}
static FxResult start_module(FxSettings *settings,FxC110Demo **out,void *page,void *text_page,uint32_t selection,
                             const FxDiscoveredModule *entry,uint32_t owner) {
    if((!settings && !entry) || !out || *out || !page || !text_page ||
       (settings && settings->stage!=FX_PAGE_DORMANT)) return FX_STATE;
    uint32_t package=entry?entry->bytes:16384u; /* Small controller scratch; large preflight allocates temporarily. */
    uint32_t image=entry?entry->image_bytes:131072u;
    if(package>PACKAGE_BYTES || !image || image>IMAGE_BYTES)return FX_BOUNDS;
    FxExecLayout layout;
    uint32_t extra=0u;
#ifdef FX_C110_FAULT_TEST
    if(!entry)extra=4096u+(uint32_t)align(sizeof(((FxC110Demo *)0)->draw_commands[0]),4096u);
#endif
    FxModuleStacks stacks=entry?entry->stacks:(FxModuleStacks){FX_MODULE_STACK_DEFAULT,FX_MODULE_STACK_DEFAULT};
    FxResult planned;
#ifndef FX_C110_FAULT_TEST
    if(!entry) {
        /* This controller never executes module code: one native stack only.
         * Private tables/API/EL0 stack/image are allocated by child workers. */
        layout=(FxExecLayout){.native_stack=(uint32_t)align(sizeof(FxC110Demo),4096u),
            .native_stack_bytes=stacks.native_bytes,.package_bytes=package};
        layout.bytes=layout.native_stack+layout.native_stack_bytes;planned=FX_OK;
    } else
#endif
        planned=fx_exec_layout_stacks((uint32_t)sizeof(FxC110Demo),package,image,FX_SERVICE_EXCHANGE_BYTES,extra,stacks,&layout);
    if(planned!=FX_OK)return planned;
    FxAllocationObserver controller=entry?(FxAllocationObserver){0}:fx_account_observer(controller_account("loader.storage"));
    void *allocation=FN(UINT64_C(0x40f41084),void *(*)(uint32_t,uint32_t))(layout.bytes+4096u,0u);
    if(!allocation) return FX_BUDGET;
    fx_allocation_observe(controller,layout.bytes+4096u,FX_ALLOC_REQUESTED);
    uintptr_t base=align((uintptr_t)allocation,4096u);
    FxC110Demo *d=(FxC110Demo *)base;
    memset(d,0,sizeof(*d));d->panic.module_result=INT32_MIN;d->allocation=allocation;d->base=base;d->end=base+layout.bytes;d->layout=layout;
    d->generic=entry!=0;d->owner_id=owner;
    if(entry)d->entry=*entry;
    d->ui_settings=settings?settings:&d->owned_settings;d->module_selection=selection;
    d->page=page;
    d->text_page=text_page;
    d->settings=&d->owned_settings;
    fx_settings_init(d->settings);
    d->table=layout.tables?(uint64_t (*)[512])(base+layout.private_tables):0;
    d->user_table=layout.tables?(uint64_t (*)[512])(base+layout.user_tables):0;
    d->stack=(uint8_t *)(base+layout.native_stack);
    if(layout.tables) {
        d->user_services=(FxFaultServices *)(base+layout.api);d->exchange=(FxServiceExchange *)(base+layout.exchange);
        d->user_stack=(uint8_t *)(base+layout.user_stack);d->image=(uint8_t *)(base+layout.image);
    }
#ifdef FX_C110_FAULT_TEST
    d->user_stats=(FxDemoStats *)d->exchange;
    d->user_scene=(FxDemoScene *)(base+layout.extra);d->user_commands=(FxDemoDraw *)(base+layout.extra+4096u);
    for(uint32_t i=0u;i<2u;++i)
        d->scene[i]=(FxDemoScene){1280u,800u,FX_DEMO_DRAW_CAPACITY,0u,d->draw_commands[i],0,0u,0u};
    d->services=(FxDemoServices){{1u,sizeof(FxDemoServices),d,0},0,0u,0u,&d->stats,&d->scene[0]};
    d->services.base.bytes=sizeof(FxFaultServices);
#else
    if(!entry) {
        d->panic_page=FN(UINT64_C(0x40f41084),FxNativePanicPage *(*)(uint32_t,uint32_t))((uint32_t)sizeof(*d->panic_page),0u);
        if(!d->panic_page){FN(UINT64_C(0x40f410c4),void (*)(void *))(allocation);fx_allocation_observe(controller,layout.bytes+4096u,FX_ALLOC_FREE_PENDING);return FX_BUDGET;}
        fx_allocation_observe(controller,(uint32_t)sizeof(*d->panic_page),FX_ALLOC_REQUESTED);
        fx_native_panic_page_init(d->panic_page);
        fx_allocation_observe(controller,(uint32_t)sizeof(*d->panic_page),FX_ALLOC_PUBLISHED);
    }
#endif
    if(entry) {
        memset(d->exchange,0,sizeof(*d->exchange));
        memcpy(d->native.id,entry->id,sizeof(entry->id));
        fx_service_init(&d->service,owner,1u,(entry->required|entry->optional)&FX_SDK_CAPS,
            (FxServiceBackend){&d->native,fx_c110_service_call,fx_c110_service_stop});
        d->simd_enabled=(d->service.capabilities&FX_SDK_SIMD)?1u:0u;
        d->native.owner=owner;d->native.generation=1u;d->native.capabilities=d->service.capabilities;
        d->native.module=&d->extension.loader.module;d->native.module_image=d->image;
#ifdef FX_PLATFORM_PROFILE
        (void)fx_worker_wait_init(&d->wait,fx_platform_target(),entry->period_ms);d->native.worker_wait=&d->wait;
        /* A failed load can park before its lazy PlatformOwner exists. Its
         * retained native wait flag still belongs to the activated domain. */
        d->wait.event_capacity=fx_capacity_find(fx_native_capacity_registry(),"rtos.events");
#endif
        d->service.wake_context=&d->wait;d->service.wake_callback=fx_worker_wait_signal;
        uint32_t id=0;
        FxResult r=fx_service_region(&d->service,"module.scratch",d->service_memory,sizeof(d->service_memory),7u,&id);
        if(r!=FX_OK){FN(UINT64_C(0x40f410c4),void (*)(void *))(allocation);return r;}
        (void)fx_service_event(&d->service,FX_EVENT_INIT,0u);
        FxResult account=fx_runtime_accounts_init(&d->accounting,owner,1u,(FxIndexMemory){0,account_allocate,load_release});
        if(account!=FX_OK){FN(UINT64_C(0x40f410c4),void (*)(void *))(allocation);return account;}
        (void)fx_account_acquire(&d->accounting.allocation,layout.bytes+4096u,1u,1u);
        (void)fx_module_load_buffer_account(&d->load_buffer,&d->accounting.source);
        d->native.accounting=&d->accounting;
    }
    else {FxAccountRow *source=controller_account("module.inspect");if(source)(void)fx_module_load_buffer_account(&d->load_buffer,source);}
    d->public.magic=0x46584431u;d->public.version=1u;
    /* C1.10 kernel record from its own normal task factory (40f4210c).
     * The stack is explicitly owned; no existing task or saved frame is used. */
    NativeTask task={0u,0u,(uintptr_t)d,(uintptr_t)worker,28u,0u,layout.native_stack_bytes,
        (uintptr_t)d->stack,1u,UINT32_MAX};
    fx_allocation_observe(controller,layout.bytes+4096u,FX_ALLOC_PUBLISHED);
    int32_t id=FN(UINT64_C(0x4018507c),int32_t (*)(const NativeTask *))(&task);
    d->public.task_id=id;
    if(id<=0) { fail(d,FX_CAPABILITY);*out=d;return FX_CAPABILITY; }
    *out=d;
    int32_t started=FN(UINT64_C(0x401855f8),int32_t (*)(int32_t))(id);
    if(started<0) { fail(d,FX_STATE);return FX_STATE; }
    return FX_OK;
}
FxResult fx_c110_demo_start(FxSettings *s,FxC110Demo **out,void *page,void *text,uint32_t selection) {
    return start_module(s,out,page,text,selection,0,0u);
}
void fx_c110_demo_observer_fault(FxC110Demo *d,FxResult error) {
    if(!d)return;
    uint32_t empty=0u;
    (void)atomic_compare_exchange_strong_explicit(&d->observer_fault,&empty,(uint32_t)(error==FX_OK?FX_INTEGRITY:error),memory_order_release,memory_order_relaxed);
    fx_worker_wait_signal(&d->wait);
}
uint32_t fx_c110_module_menu(FxC110Demo *d,uint32_t index,char label[32],uint32_t *value,int *enabled) {
    if(!d || index>=atomic_load(&d->child_count))return 0;
    FxC110Demo *child=atomic_load_explicit(&d->children[index],memory_order_acquire);
    if(!child || !atomic_load_explicit(&child->native.menu_ready,memory_order_acquire))return 0;
    memcpy(label,child->native.menu_label,32u);*value=atomic_load(&child->native.menu_value);
    *enabled=fx_c110_demo_phase(d)!=PANICKED && fx_c110_demo_phase(child)==ACTIVE && !atomic_load(&d->stopping);
    return 1u;
}
FxResult fx_c110_module_action(FxC110Demo *d,uint32_t index) {
    if(!d || index>=atomic_load(&d->child_count) || fx_c110_demo_phase(d)==PANICKED || atomic_load(&d->stopping))return FX_STATE;
    FxC110Demo *child=atomic_load_explicit(&d->children[index],memory_order_acquire);
    if(!child || fx_c110_demo_phase(child)!=ACTIVE || atomic_load(&child->stopping))return FX_STATE;
    uint32_t empty=0;
    if(!atomic_compare_exchange_strong(&child->native.menu_action,&empty,1u))return FX_BUSY;
    fx_worker_wait_signal(&child->wait);return FX_OK;
}
FxPlatformOwner *fx_c110_module_platform(FxC110Demo *d,uint32_t index) {
    if(!d || index>=atomic_load_explicit(&d->child_count,memory_order_acquire))return 0;
    FxC110Demo *child=atomic_load_explicit(&d->children[index],memory_order_acquire);
    return child?atomic_load_explicit(&child->native.platform,memory_order_acquire):0;
}
uint32_t fx_c110_module_count(const FxC110Demo *d){return d?atomic_load_explicit(&d->child_count,memory_order_acquire):0u;}
uint32_t fx_c110_module_capacity(const FxC110Demo *d){return d && atomic_load_explicit(&d->catalog_loaded,memory_order_acquire)?d->children_capacity:0u;}
const char *fx_c110_module_identity(const FxC110Demo *d,uint32_t index) {
    if(!d || index>=atomic_load_explicit(&d->child_count,memory_order_acquire))return 0;
    FxC110Demo *child=atomic_load_explicit(&d->children[index],memory_order_acquire);
    return child?child->entry.id:0;
}
const uint32_t fx_c110_services_metadata[]={0x46585332u,2u,offsetof(FxC110Demo,children),
    offsetof(FxC110Demo,child_count),offsetof(FxC110Demo,service),sizeof(FxServiceOwner),
    offsetof(FxC110Demo,native),offsetof(FxC110Demo,exchange),offsetof(FxC110Demo,entry),
    offsetof(FxServiceOwner,last_log),offsetof(FxC110Services,menu_ready),offsetof(FxC110Services,native_calls)};
FxResult fx_c110_demo_request(FxC110Demo *d,uint32_t command) {
#ifndef FX_C110_FAULT_TEST
    (void)d;(void)command;return FX_CAPABILITY;
#else
    if(!d || command<FX_C110_PLAY || command>FX_C110_RETRY_CARD) return FX_STATE;
    if(command==FX_C110_DISABLE || atomic_load(&d->stopping)) return FX_STATE;
    if((command==FX_C110_PLAY || command==FX_C110_TEST_FAULT) && !(d->module_selection&FX_STARTUP_DEMO))return FX_CAPABILITY;
#ifndef FX_C110_FAULT_TEST
    if(command==FX_C110_TEST_FAULT) return FX_STATE;
#endif
    uint32_t current=fx_c110_demo_phase(d);
    if(command==FX_C110_RETRY_CARD && current!=CARD_WAIT) return FX_STATE;
    if(current==CARD_WAIT && command!=FX_C110_RETRY_CARD) return FX_STATE;
    if(current==PANICKED || current==DISABLED) {
        atomic_fetch_add_explicit(&d->panic.rejected_commands,1u,memory_order_relaxed);
        /* Acknowledging an action must not dismiss the latched fault page. */
        return FX_STATE;
    }
    if(command==FX_C110_WRITE_TEXT) {
        uint32_t phase=fx_c110_demo_phase(d);
        if(phase!=ACTIVE && phase!=FINISHED) return FX_STATE;
    }
    uint32_t empty=0u;
    if(!atomic_compare_exchange_strong_explicit(&d->public.command,&empty,command,
        memory_order_release,memory_order_relaxed)) return FX_BUSY;
    return FX_OK;
#endif
}
uint32_t fx_c110_demo_text_state(const FxC110Demo *d) {
    return d?atomic_load_explicit(&d->text_state,memory_order_acquire):FX_TEXT_IDLE;
}
uint32_t fx_c110_demo_phase(const FxC110Demo *d) {
    return d?atomic_load_explicit(&d->public.phase,memory_order_acquire):QUEUED;
}
/* Regions are retained Loader buffers, never addresses supplied by USB.
 * Another module's image can change concurrently: this is an explicitly
 * non-atomic debug read, not a lease or a coherent module checkpoint. */
static int debug_region(FxC110Demo *d,uint32_t id,const uint8_t **start,uint32_t *size) {
#ifndef FX_C110_FAULT_TEST
    if(!id || id>2u*atomic_load_explicit(&d->child_count,memory_order_acquire))return 0;
    uint32_t slot=(id-1u)/2u;
    if(slot>=atomic_load_explicit(&d->child_count,memory_order_acquire))return 0;
    d=atomic_load_explicit(&d->children[slot],memory_order_acquire);if(!d)return 0;
    uint32_t phase=atomic_load_explicit(&d->public.phase,memory_order_acquire);
    if(phase!=ACTIVE && phase!=FINISHED)return 0;
    id=(id-1u)%2u+1u;
#endif
    if(!d->extension.loader.module.prepared) return 0;
    /* Odd package IDs are retired holes; source scratch has been released. */
    if(id==2u) { *start=d->image;*size=d->extension.loader.module.bytes; }
    else return 0;
    uintptr_t p=(uintptr_t)*start;
    return *size && *size<=d->layout.image_bytes &&
        p>=d->base && p<=d->end && (uintptr_t)*size<=d->end-p;
}
uint32_t fx_c110_demo_debug(FxC110Demo *d,uint16_t op,const uint8_t *in,uint32_t bytes,
                            uint8_t *out,uint32_t *count) {
    *count=0;
    if(op<FX_PTP_DIAGNOSTICS || op>FX_PTP_READ_REGION) return FX_PTP_UNSUPPORTED;
    if(!d) return FX_PTP_CLOSED;
    uint32_t phase=fx_c110_demo_phase(d);
    if(phase!=ACTIVE && phase!=PLAYING && phase!=FINISHED) return FX_PTP_CLOSED;
    if(op==FX_PTP_DIAGNOSTICS) {
        if(bytes) return FX_PTP_ARGUMENT;
        memset(out,0,128u);memcpy(out,"FXD1",4u);fx_ptp_put32(out+4,1u);
        fx_ptp_put32(out+8,128u);fx_ptp_put32(out+12,phase);
        fx_ptp_put64(out+16,now());
        fx_ptp_put32(out+24,atomic_load(&d->public.error));
        fx_ptp_put32(out+28,(uint32_t)d->public.task_id);
        fx_ptp_put32(out+32,atomic_load(&d->public.frame));
        fx_ptp_put32(out+36,atomic_load(&d->public.invocations));
        fx_ptp_put32(out+40,d->public.context_checks);
        fx_ptp_put32(out+44,d->public.context_errors);
        fx_ptp_put32(out+48,d->user_entries);fx_ptp_put32(out+52,d->user_exits);
        fx_ptp_put32(out+56,d->public.faults);
        fx_ptp_put32(out+60,d->card_load_attempts);
        fx_ptp_put64(out+64,d->public.esr);fx_ptp_put64(out+72,d->public.elr);
        fx_ptp_put64(out+80,d->public.far);
        fx_ptp_put32(out+88,atomic_load(&d->text_state));
        fx_ptp_put32(out+92,d->extension.loader.runtime.generation);
        fx_ptp_put32(out+96,d->extension.loader.runtime.outstanding);
        fx_ptp_put32(out+100,d->extension.loader.module.bytes);
        fx_ptp_put32(out+104,d->extension.loader.module.entry_offset);
        fx_ptp_put32(out+108,FX_C110_DEMO_PACKAGE_SIZE);
        fx_ptp_put32(out+112,FX_PTP_CAPABILITIES);
        *count=128u;return FX_PTP_OK;
    }
    if(op==FX_PTP_REGIONS) {
#ifndef FX_C110_FAULT_TEST
        if(bytes==4u) {
            uint32_t cursor=fx_ptp_get32(in),total=2u*atomic_load_explicit(&d->child_count,memory_order_acquire),rows=0u;
            if(cursor>total)return FX_PTP_ARGUMENT;
            memset(out,0,FX_PTP_PAYLOAD);memcpy(out,"FXR3",4u);fx_ptp_put32(out+4,3u);
            fx_ptp_put32(out+12,32u);fx_ptp_put32(out+16,total);
            while(cursor<total && rows<(FX_PTP_PAYLOAD-32u)/32u) {
                uint32_t id=++cursor;const uint8_t *start;uint32_t size;
                if(!debug_region(d,id,&start,&size))continue;
                uint32_t kind=(id-1u)%2u;uint8_t *row=out+32u+rows++*32u;
                fx_ptp_put32(row,id);fx_ptp_put32(row+4,1u);fx_ptp_put32(row+8,size);
                fx_ptp_put32(row+12,kind);fx_ptp_put64(row+16,(uintptr_t)start);
                fx_ptp_put32(row+24,(id-1u)/2u+1u);fx_ptp_put32(row+28,kind);
            }
            fx_ptp_put32(out+8,rows);fx_ptp_put32(out+20,cursor<total?cursor:UINT32_MAX);
            *count=32u+rows*32u;return FX_PTP_OK;
        }
        uint32_t slots=2u*atomic_load_explicit(&d->child_count,memory_order_acquire);
        if(bytes || slots>8u)return FX_PTP_ARGUMENT;
        memset(out,0,16u+slots*32u);memcpy(out,"FXR2",4u);
        fx_ptp_put32(out+4,2u);fx_ptp_put32(out+12,32u);uint32_t rows=0u;
        for(uint32_t id=1u;id<=slots;++id) {
            const uint8_t *start;uint32_t size;if(!debug_region(d,id,&start,&size))continue;
            uint32_t kind=(id-1u)%2u;uint8_t *row=out+16u+rows++*32u;
            fx_ptp_put32(row,id);fx_ptp_put32(row+4,1u);fx_ptp_put32(row+8,size);
            fx_ptp_put32(row+12,kind);fx_ptp_put64(row+16,(uintptr_t)start);
            fx_ptp_put32(row+24,(id-1u)/2u+1u);fx_ptp_put32(row+28,kind);
        }
        fx_ptp_put32(out+8,rows);*count=16u+rows*32u;return FX_PTP_OK;
#else
        if(bytes)return FX_PTP_ARGUMENT;
        memset(out,0,80u);memcpy(out,"FXR1",4u);
        fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,2u);fx_ptp_put32(out+12,32u);
        for(uint32_t id=1u;id<=2u;++id) {
            const uint8_t *start;uint32_t size;
            if(!debug_region(d,id,&start,&size)) return FX_PTP_INTEGRITY;
            uint8_t *row=out+16u+(id-1u)*32u;
            fx_ptp_put32(row,id);fx_ptp_put32(row+4,1u); /* read-only */
            fx_ptp_put32(row+8,size);fx_ptp_put32(row+12,id==1u?0u:1u); /* volatile image */
            fx_ptp_put64(row+16,(uintptr_t)start); /* informational, never a request pointer */
        }
        *count=80u;return FX_PTP_OK;
#endif
    }
    if(bytes!=12u) return FX_PTP_ARGUMENT;
    uint32_t id=fx_ptp_get32(in),offset=fx_ptp_get32(in+4),n=fx_ptp_get32(in+8);
    const uint8_t *start;uint32_t size;
    if(!n || n>FX_PTP_PAYLOAD || !debug_region(d,id,&start,&size) ||
       offset>size || n>size-offset) return FX_PTP_ARGUMENT;
    memcpy(out,start+offset,n);*count=n;return FX_PTP_OK;
}
void fx_c110_demo_page_closed(FxC110Demo *d) {
    if(!d) return;
    /* Native page leave callback runs before the native surface is retired. */
    display_restore(d);panic_detach(d);
    if(fx_c110_demo_phase(d)==PLAYING) (void)fx_c110_demo_request(d,FX_C110_STOP);
}
const uint32_t fx_native_load_buffer_metadata[]={0x46584c42u,1u,
    offsetof(FxC110Demo,load_buffer),offsetof(FxModuleLoadBuffer,data),offsetof(FxModuleLoadBuffer,bytes),
    offsetof(FxModuleLoadBuffer,peak),offsetof(FxModuleLoadBuffer,released_bytes),offsetof(FxModuleLoadBuffer,releases),
    offsetof(FxModuleLoadBuffer,retained),offsetof(FxC110Demo,layout)+offsetof(FxExecLayout,bytes),
    offsetof(FxC110Demo,layout)+offsetof(FxExecLayout,package_bytes),sizeof(FxModuleLoadBuffer),
    offsetof(FxC110Demo,extension)+offsetof(FxExtension,loader)+offsetof(FxLoader,runtime)+offsetof(FxRuntime,reserved_bytes),
    offsetof(FxC110Demo,extension)+offsetof(FxExtension,loader)+offsetof(FxLoader,source_reserved),
    offsetof(FxC110Demo,extension)+offsetof(FxExtension,loader)+offsetof(FxLoader,card)+offsetof(FxCardReceipt,verified)};
const uint32_t fx_native_load_owner_metadata[]={0x46584c4fu,1u,UINT32_MAX};
#ifdef FX_NATIVE_FILE_TEST_BACKEND
#define WORKER_NVME_OFFSET offsetof(FxC110Demo,nvme)
#else
#define WORKER_NVME_OFFSET UINT32_MAX
#endif
#ifdef FX_C110_FAULT_TEST
#define WORKER_DISPLAY_VERSION 1u
#define WORKER_DISPLAY_OFFSET offsetof(FxC110Demo,display)
#define WORKER_PANIC_VERSION 2u
#define WORKER_PANIC_FRAME (offsetof(FxC110Demo,services)+offsetof(FxDemoServices,frame))
#define WORKER_PANIC_DISPLAY offsetof(FxC110Demo,panic_display)
#define WORKER_FAULT_ARMED (offsetof(FxC110Demo,fault_services)+offsetof(FxFaultServices,armed))
#define WORKER_USER_SCENE offsetof(FxC110Demo,user_scene)
#define WORKER_USER_COMMANDS offsetof(FxC110Demo,user_commands)
#else
#define WORKER_DISPLAY_VERSION 2u
#define WORKER_DISPLAY_OFFSET offsetof(FxC110Demo,panic_page)
#define WORKER_PANIC_VERSION 3u
#define WORKER_PANIC_FRAME (offsetof(FxC110Demo,public)+offsetof(Public,frame))
#define WORKER_PANIC_DISPLAY offsetof(FxC110Demo,panic_page)
#define WORKER_FAULT_ARMED UINT32_MAX
#define WORKER_USER_SCENE UINT32_MAX
#define WORKER_USER_COMMANDS UINT32_MAX
#endif
const uint32_t fx_c110_demo_metadata[]={0x46584431u,2u,sizeof(FxC110Demo),WORKER_NVME_OFFSET,
    offsetof(FxC110Demo,extension),offsetof(FxC110Demo,arena),offsetof(FxC110Demo,table_count)};
const uint32_t fx_c110_debug_metadata[]={0x46584442u,1u,sizeof(FxC110Demo),
    offsetof(FxC110Demo,base),offsetof(FxC110Demo,end),offsetof(FxC110Demo,package),
    offsetof(FxC110Demo,image),
    offsetof(FxC110Demo,extension)+offsetof(FxExtension,loader)+offsetof(FxLoader,module)+offsetof(FxModule,prepared),
    offsetof(FxC110Demo,extension)+offsetof(FxExtension,loader)+offsetof(FxLoader,module)+offsetof(FxModule,bytes),
    offsetof(FxC110Demo,public)+offsetof(Public,phase)};
const uint32_t fx_c110_demo_display_metadata[]={0x46584731u,WORKER_DISPLAY_VERSION,WORKER_DISPLAY_OFFSET,
    sizeof(FxC110Display),offsetof(FxC110Display,width),offsetof(FxC110Display,original_objects),
    offsetof(FxC110Display,original_object_count)};
const uint32_t fx_c110_panic_metadata[]={0x46585031u,WORKER_PANIC_VERSION,
    offsetof(FxC110Demo,panic),
    offsetof(FxC110Demo,extension)+offsetof(FxExtension,loader)+offsetof(FxLoader,runtime),
    offsetof(FxRuntime,first_fault),offsetof(FxRuntime,fault_count),
    offsetof(FxC110Demo,arena)+offsetof(FxExecArena,bound),
    offsetof(FxC110Demo,extension)+offsetof(FxExtension,debug)+offsetof(FxDebug,enabled),
    offsetof(FxC110Demo,allocation),offsetof(FxC110Demo,image),
    WORKER_PANIC_FRAME,WORKER_PANIC_DISPLAY,offsetof(FxC110Display,width),WORKER_FAULT_ARMED};
const uint32_t fx_c110_demo_text_metadata[]={0x46585431u,1u,offsetof(FxC110Demo,text),sizeof(FxTextReport),
    offsetof(FxC110Demo,text_refresh)};
const uint32_t fx_c110_storage_metadata[]={0x46585332u,1u,offsetof(FxC110Demo,native_file),
    sizeof(FxFileCard),offsetof(FxC110Demo,card_load_attempts)};
const uint32_t fx_c110_isolation_metadata[]={0x46584931u,1u,
    offsetof(FxC110Demo,user_last_spsr),offsetof(FxC110Demo,user_entries),
    offsetof(FxC110Demo,user_table),offsetof(FxC110Demo,user_returns),
    offsetof(FxC110Demo,user_stack),offsetof(FxC110Demo,user_services),
    WORKER_USER_SCENE,WORKER_USER_COMMANDS};
const uint32_t fx_c110_simd_metadata[]={0x46585349u,1u,
    offsetof(FxC110Demo,simd_enabled),offsetof(FxC110Demo,module_simd),sizeof(FxSimdState),
    offsetof(FxC110Demo,image),offsetof(FxC110Demo,user_context)+256u};
const uint32_t fx_c110_preemption_metadata[]={0x46584932u,1u,
    offsetof(FxC110Demo,user_context),offsetof(FxC110Demo,invocation_deadline),100u};
#ifdef FX_C110_FAULT_TEST
const uint32_t fx_c110_shooting_metadata[]={0x46585331u,2u,
    offsetof(FxC110Demo,background_armed),offsetof(FxC110Demo,panic_display)+offsetof(FxC110Display,application)};
#else
const uint32_t fx_c110_shooting_metadata[]={0x46585331u,3u,
    offsetof(FxC110Demo,background_armed),offsetof(FxC110Demo,panic_page),offsetof(FxNativeDisplay,application)};
#endif
#ifdef FX_NATIVE_FILE_TEST_BACKEND
const uint32_t fx_c110_file_test_metadata[]={0x46584631u,1u,offsetof(FxC110Demo,file_test_card),
    sizeof(FxC110FileTestCard),offsetof(FxC110FileTestCard,mounted)};
#endif

int fx_c110_demo_shutdown(FxC110Demo *d) {
    if(!d)return 1;
    uint32_t expected=0u;
    if(atomic_compare_exchange_strong_explicit(&d->stopping,&expected,1u,memory_order_release,memory_order_relaxed))fx_worker_wait_signal(&d->wait);
    /* Only mark existing child owners; no native close/call/lease release
     * runs on this caller. Each worker drains on its own normal stack. */
    for(uint32_t i=0;i<atomic_load_explicit(&d->child_count,memory_order_acquire);++i)
        (void)fx_c110_demo_shutdown(atomic_load_explicit(&d->children[i],memory_order_acquire));
    return (int)atomic_load_explicit(&d->stopped,memory_order_acquire);
}
