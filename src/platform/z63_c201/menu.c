#include "fenestra_native_menu.h"
#include "fenestra_native_font.h"
#include "fenestra_memory.h"
#include "lifecycle.h"
#ifdef FX_Z63_SDK
#include "fenestra_hooks.h"
#include "fenestra_native_mapping.h"
#include "fenestra_module_bus.h"
#endif
#ifdef FX_Z63_PTP
#include "ptp.h"
#include "fenestra_native_rtos.h"
#include "fenestra_native_linux.h"
#include "task.h"
#endif
#ifdef FX_Z63_RUNTIME
#include "runtime.h"
#include "startup.h"
#endif
#ifdef FX_Z63_UI
#include "ui.h"
#endif
#define TEXT 3285u
#define PAGE 0x7000u
#define MAGIC 0x465a634du
typedef struct {
    FxNativeDescriptor descriptor;
    uint32_t magic,installed;
    FxZ63Lifecycle lifecycle;
    _Atomic uint32_t activated;
    FxNativeRow rows[51];uintptr_t events[51];
    FxNativeEvent entry[18],navigation[18];
    FxNativeMenu menu;
    FxNativeTextBank *bank;
#ifdef FX_Z63_UI
    FxZ63Ui *ui;
#endif
#ifdef FX_Z63_RUNTIME
    _Atomic(FxZ63Runtime *) runtime;
    FxZ63Startup startup;
    uint16_t boot_text[3][64];
#endif
#ifdef FX_Z63_PTP
    FxPtp ptp;FxRtos rtos;FxLinuxBridge linux;FxZ63TaskScope debug_task;_Atomic uint32_t debug_started;
#endif
#ifdef FX_Z63_SDK
    FxHookRegistry hooks;FxNativeMappingRegistry mappings;_Atomic(FxModuleBus *) bus;
#endif
} State;
#ifdef FX_Z63_UI
const uint32_t fx_z63_menu_ui_offset=offsetof(State,ui);
const uint32_t fx_z63_menu_runtime_offset=offsetof(State,runtime);
#endif
static FxNativePage *setup(void){return (FxNativePage *)(uintptr_t)UINT64_C(0xa02912e10);}
static State *state(void) {
    uintptr_t p=setup()->descriptor;
    if(p==UINT64_C(0xa028d5590) || (p&7u) || p<UINT64_C(0xa00000000) || p>UINT64_C(0xa40000000)-sizeof(State))return 0;
    State *s=(State *)p;return s->magic==MAGIC && s->installed?s:0;
}
static uint32_t noop(void *context){(void)context;return 1u;}
extern void fx_z63_probe_module_start(void);
#ifdef FX_Z63_RUNTIME
const uint32_t fx_z63_startup_layout[]={0x46584236u,1u,offsetof(State,startup),
    offsetof(FxZ63Startup,phase),offsetof(FxZ63Startup,desired),offsetof(FxZ63Startup,error),
    offsetof(FxZ63Startup,shutdown_done)};
static FxResult start_modules(void *context) {
    State *s=context;
    if(s!=state() || !fx_z63_lifecycle_accepts_work(&s->lifecycle) || atomic_load(&s->activated))return FX_STATE;
#ifdef FX_Z63_UI
    FxResult r=fx_z63_ui_freeze();if(r!=FX_OK)return r;
#endif
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong(&s->activated,&zero,1u))return FX_STATE;
    fx_z63_probe_module_start();
    return atomic_load_explicit(&s->runtime,memory_order_acquire)?FX_OK:FX_BUDGET;
}
static uint32_t runtime_state(void *context) {
    State *s=context;return fx_z63_runtime_state(atomic_load_explicit(&s->runtime,memory_order_acquire));
}
static uint32_t boot_command(uint32_t command) {
    State *s=state();if(s && fx_z63_lifecycle_accepts_work(&s->lifecycle))
        (void)fx_z63_startup_request(&s->startup,command);
    return 1u;
}
static uint32_t auto_toggle(void *context){(void)context;return boot_command(FX_Z63_BOOT_AUTO);}
static uint32_t guard_clear(void *context){(void)context;return boot_command(FX_Z63_BOOT_CLEAR);}
#endif
static uint32_t activate(void *context) {
#ifdef FX_Z63_RUNTIME
    (void)context;return boot_command(FX_Z63_BOOT_ACTIVATE);
#else
    (void)context;State *s=state();
    if(!s || !fx_z63_lifecycle_accepts_work(&s->lifecycle) ||
       atomic_load_explicit(&s->activated,memory_order_acquire))return 1u;
    #ifdef FX_Z63_UI
    if(fx_z63_ui_freeze()!=FX_OK)return 1u;
#endif
    uint32_t zero=0;
    if(atomic_compare_exchange_strong(&s->activated,&zero,1u))fx_z63_probe_module_start();
    return 1u;
#endif
}
static void *allocate(void *context,uint32_t bytes) {
    (void)context;return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u)(bytes,0u);
}
static void release(void *context,void *p) {
    (void)context;((void (*)(void *))(uintptr_t)0x40d32d40u)(p);
}
#ifdef FX_Z63_SDK
FxHookRegistry *fx_native_hooks(void){State *s=state();return s?&s->hooks:0;}
FxNativeMappingRegistry *fx_native_mappings(void){State *s=state();return s?&s->mappings:0;}
FxModuleBus *fx_native_module_bus(void) {
    State *s=state();if(!s)return 0;
    FxModuleBus *b=atomic_load_explicit(&s->bus,memory_order_acquire);if(b)return b;
    b=allocate(0,(uint32_t)sizeof(*b));if(!b)return 0;memset(b,0,sizeof(*b));
    if(fx_module_bus_init(b,(FxIndexMemory){0,allocate,release})!=FX_OK){release(0,b);return 0;}
    FxModuleBus *zero=0;
    if(!atomic_compare_exchange_strong_explicit(&s->bus,&zero,b,memory_order_release,memory_order_acquire)){release(0,b);return zero;}
    return b;
}
#endif
#ifdef FX_Z63_PTP
FxLinuxBridge *fx_z63_menu_linux(void){State *s=state();return s?&s->linux:0;}
FxRtos *fx_z63_menu_rtos(void){State *s=state();return s?&s->rtos:0;}
FxPtp *fx_z63_menu_ptp(void){State *s=state();return s?&s->ptp:0;}
int fx_z63_menu_ptp_active(void){State *s=state();return s && fx_z63_lifecycle_accepts_work(&s->lifecycle);}
extern void fx_z63_probe_module_result(uint32_t,uint32_t,uint64_t);
static void usb_task_status(void) {
    uint64_t info[32]={0};int32_t id=*(const volatile int32_t *)(uintptr_t)UINT64_C(0xa0305c700);
    int32_t r=((int32_t (*)(int32_t,void *))(uintptr_t)0x4018c8ccu)(id,info);
    if(!r)for(uint32_t i=0;i<9u;++i)fx_z63_probe_module_result(74u,i,info[i]);
}
static void debug_run(void *context) {
    State *s=context;uint32_t polls=0u;
    while(fx_z63_lifecycle_accepts_work(&s->lifecycle)) {
        fx_z63_ptp_poll();
        if(++polls==1u || polls==1000u)usb_task_status();
        if(fx_z63_task_delay_ticks(10u)!=FX_OK){fx_ptp_enable(&s->ptp,0);return;}
    }
    fx_ptp_enable(&s->ptp,0);
}
extern void fx_z63_probe_module_result(uint32_t,uint32_t,uint64_t);
static uint32_t debug_toggle(void *context) {
    (void)context;State *s=state();if(!s || !fx_z63_menu_ptp_active())return 1u;
    fx_z63_probe_module_result(70u,*(const volatile uint8_t *)(uintptr_t)UINT64_C(0xa0305c639),atomic_load(&s->ptp.enabled));
    if(atomic_load(&s->ptp.enabled)){fx_ptp_enable(&s->ptp,0);fx_linux_revoke();return 1u;}
    /* The C2.01 stock baseline leaves the USB service stopped in these
     * inputs. Explicit debugging requests the original normal Start method;
     * it does not change persistent USB/network settings or maintenance mode.
     * Turning Fenestra off revokes its endpoint, leaving native USB ownership. */
    /* Resume the already-created original USB service through its normal
     * start method only when the kernel reports it dormant. Never construct
     * another native service object or infer dormancy from queued messages. */
    uintptr_t service=UINT64_C(0xa0305c700);
    int32_t id=*(const volatile int32_t *)service;
    uint64_t status[32]={0};
    if(id<=0 || *(const volatile uintptr_t *)(service+8u)!=UINT64_C(0x40c0025c) ||
       *(const volatile uintptr_t *)(service+0x48u)!=UINT64_C(0x40321d7c))return 1u;
    int32_t queried=((int32_t (*)(int32_t,void *))(uintptr_t)0x4018c8ccu)(id,status);
    fx_z63_probe_module_result(72u,(uint32_t)queried,status[0]&UINT32_MAX);
    if(queried)return 1u;
    if((uint32_t)status[0]==0x10u) {
        if(((uint32_t (*)(void))(uintptr_t)0x40320320u)())return 1u;
        memset(status,0,sizeof(status));
        queried=((int32_t (*)(int32_t,void *))(uintptr_t)0x4018c8ccu)(id,status);
        fx_z63_probe_module_result(73u,(uint32_t)queried,status[0]&UINT32_MAX);
        if(queried || (uint32_t)status[0]==0x10u)return 1u;
    }
    if(!*(const volatile uint8_t *)(uintptr_t)UINT64_C(0xa0305c639) &&
       ((uint32_t (*)(void))(uintptr_t)0x40320b04u)())return 1u;
    if(!*(const volatile uint8_t *)(uintptr_t)UINT64_C(0xa0305c622)) {
        int32_t timer=*(const volatile int32_t *)(uintptr_t)UINT64_C(0xa0305c598);
        uintptr_t callback=*(const volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305c5a0);
        if(timer<=0 || callback!=UINT64_C(0x403204d0))return 1u;
        (void)((uint32_t (*)(void))(uintptr_t)0x403214ccu)();
    }
    uint32_t zero=0;
    if(atomic_compare_exchange_strong(&s->debug_started,&zero,1u)) {
        void *stack=allocate(0,65536u+16u);if(!stack)return 1u;
        stack=(void *)(((uintptr_t)stack+15u)&~(uintptr_t)15u);
        if(fx_z63_task_start(&s->debug_task,debug_run,s,stack,65536u)!=FX_OK)return 1u;
    }
    if(s->debug_task.phase==FX_Z63_TASK_RUNNING)fx_ptp_enable(&s->ptp,1);
    fx_z63_probe_module_result(71u,*(const volatile uint8_t *)(uintptr_t)UINT64_C(0xa0305c639),atomic_load(&s->ptp.enabled));
    return 1u;
}
#endif
#ifdef FX_Z63_UI
FxZ63Ui *fx_z63_menu_ui(void){State *s=state();return s?s->ui:0;}
FxZ63Runtime *fx_z63_menu_runtime(void){State *s=state();return s?atomic_load_explicit(&s->runtime,memory_order_acquire):0;}
FxResult fx_z63_menu_text_ensure(uint32_t n) {
    State *s=state();if(!s)return FX_STATE;FxNativeTextBank *bank=0;
    FxResult r=fx_native_text_bank_grow((FxIndexMemory){0,allocate,release},s->bank,n,&bank);
    if(r!=FX_OK || bank==s->bank)return r;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f980)=(uintptr_t)bank->fonts;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f990)=(uintptr_t)bank->attributes;
    __asm__ volatile("dmb ish":::"memory");s->bank=bank;return FX_OK;
}
#endif
void fx_z63_menu_install(void) {
    if(state())return;
    FxNativePage *page=setup();const FxNativeDescriptor *d=(const FxNativeDescriptor *)page->descriptor;
    if((page->identity>>16)!=690u || page->descriptor!=UINT64_C(0xa028d5590) ||
       d->count!=50u || d->header!=UINT64_C(0x43e2a5e0) || d->rows!=UINT64_C(0x43e2a6f0) || d->events!=UINT64_C(0x43e2af88))return;
    const FxNativeRow *rows=(const FxNativeRow *)d->rows;
    if(rows[47].word[0]!=912u)return;
    State *s=allocate(0,(uint32_t)sizeof(*s));if(!s)return;
    memset(s,0,sizeof(*s));s->descriptor=*d;
#ifdef FX_Z63_RUNTIME
    fx_z63_startup_init(&s->startup);s->startup.context=s;
    s->startup.activate=start_modules;s->startup.runtime_state=runtime_state;
#endif
    const FxNativeEvent *original=(const FxNativeEvent *)(uintptr_t)UINT64_C(0x43e5c558);
    /* The original table has 56 records. Keep the checked navigation prefix,
     * then its actual terminal; never scan it with the shared 18-record bound. */
    memcpy(s->navigation,original,14u*sizeof(*original));
    s->navigation[14]=original[55];
    if(s->navigation[14].event!=0x3f2u)return;
    uint32_t select[4]={0x2b3u,0x2b8u,0x2ccu,0x2dbu};
    FxNativeMenuProfile profile={page,&rows[47],s->navigation,0x3f2u,0x2abu,{0x2b3u,0x2b8u,0x2ccu,0x2dbu},0x712u,690u};
    uintptr_t callbacks[5]={(uintptr_t)noop,(uintptr_t)noop,UINT64_C(0x403c32c8),(uintptr_t)noop,(uintptr_t)noop};
    FxNativeEntry entries[]={{TEXT+1u,0x7ff1u,0,0,(uintptr_t)activate,0x712u},
                             {TEXT+2u,0x7ff2u,0,0,(uintptr_t)noop,690u}
#ifdef FX_Z63_PTP
                             ,{TEXT+3u,0x7ff3u,0,0,(uintptr_t)debug_toggle,0x712u}
#endif
#ifdef FX_Z63_UI
                             ,{3300u,0x7ff4u,0,0,(uintptr_t)noop,0x7001u}
                             ,{3307u,0x7ff5u,0,0,(uintptr_t)fx_z63_ui_manage,0x704au}
#endif
#ifdef FX_Z63_RUNTIME
                             ,{TEXT+4u,0x7ff6u,0,0,(uintptr_t)auto_toggle,0x712u}
                             ,{TEXT+5u,0x7ff7u,0,0,(uintptr_t)guard_clear,0x712u}
                             ,{TEXT+6u,0x7ff8u,0,0,(uintptr_t)noop,0x712u}
#endif
                             };
    if(fx_native_menu_build(&s->menu,&profile,PAGE,TEXT,entries,(uint32_t)(sizeof(entries)/sizeof(*entries)),callbacks)!=FX_OK)return;
    memcpy(s->rows+1,rows,50u*sizeof(*rows));
    memcpy(s->events+1,(const void *)d->events,50u*sizeof(uintptr_t));
    s->rows[0]=rows[47];s->rows[0].word[0]=TEXT;s->rows[0].word[2]=65535u;s->rows[0].word[6]=0x7ff0u;
    if(fx_native_menu_link(s->entry,s->navigation,0x3f2u,select,(uintptr_t)noop,PAGE)!=FX_OK)return;
    s->events[0]=(uintptr_t)s->entry;s->descriptor.rows=(uintptr_t)s->rows;s->descriptor.events=(uintptr_t)s->events;s->descriptor.count=51u;
    const uint8_t *fonts=*(const uint8_t *volatile *)(uintptr_t)UINT64_C(0xa0305f980);
    const uintptr_t *attributes=*(const uintptr_t *volatile *)(uintptr_t)UINT64_C(0xa0305f990);
    if(!fonts || !attributes || !attributes[1])return;
    if(fx_native_text_bank_create((FxIndexMemory){0,allocate,release},fonts,attributes,TEXT,29u,(TEXT+3u+(uint32_t)(sizeof(entries)/sizeof(*entries)-2u)),fonts[912],((const uint8_t *)attributes[1])[912],&s->bank)!=FX_OK)return;
#ifdef FX_Z63_PTP
    fx_z63_ptp_init(&s->ptp);
    fx_linux_account_packets(&s->linux,(FxIndexMemory){0,allocate,release},(FxAllocationObserver){0},(FxAllocationObserver){0});
#endif
#ifdef FX_Z63_UI
    FxNativeMenuProfile apps_profile=profile;apps_profile.page=&s->menu.page;apps_profile.back=PAGE;
    s->ui=fx_z63_ui_create(&apps_profile);if(!s->ui)return;
#endif
    s->magic=MAGIC;s->installed=1u;fx_z63_lifecycle_record(&s->lifecycle,20u);
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f980)=(uintptr_t)s->bank->fonts;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f990)=(uintptr_t)s->bank->attributes;
    atomic_store_explicit((_Atomic uintptr_t *)&page->descriptor,(uintptr_t)s,memory_order_release);
}
#ifdef FX_Z63_RUNTIME
void fx_z63_menu_set_runtime(FxZ63Runtime *runtime){State *s=state();if(s)atomic_store_explicit(&s->runtime,runtime,memory_order_release);}
#endif
void fx_z63_menu_lifecycle(uint32_t event) {
    State *s=state();if(!s)return;fx_z63_lifecycle_record(&s->lifecycle,event);
#ifdef FX_Z63_UI
    if(event==8u || event==24u)fx_z63_ui_shutdown();
#endif
#ifdef FX_Z63_RUNTIME
    if(event==8u || event==24u)fx_z63_runtime_stop(atomic_load_explicit(&s->runtime,memory_order_acquire));
    if(event==8u || event==24u)fx_z63_startup_shutdown(&s->startup);
    if(event==25u)(void)fx_z63_startup_start(&s->startup);
#endif
}
FxNativePage *fx_z63_menu_page(uint32_t group,uint32_t id) {
    id&=65535u;State *s=state();
    if(group==2u && id>=PAGE && id<0x9500u){
#ifdef FX_Z63_UI
        if(id!=PAGE)return fx_z63_ui_page(id);
#endif
        return s?&s->menu.page:0;
    }
    if(group>6u)return 0;
    uintptr_t root=((volatile uintptr_t *)(uintptr_t)UINT64_C(0xa03420d50))[group];
    return root?(FxNativePage *)(root+(uintptr_t)id*96u):0;
}
void fx_z63_menu_text(int32_t language,int32_t id,const uint16_t **text,uint32_t *bytes) {
    static const uint16_t title[]={'F','e','n','e','s','t','r','a'};
    static const uint16_t run[]={'S','t','a','r','t',' ','m','o','d','u','l','e','s'};
#ifdef FX_Z63_PTP
    static const uint16_t debug[]={'U','S','B',' ','d','e','b','u','g','g','i','n','g'};
#endif
    static const uint16_t back[]={'B','a','c','k'};
    if(state()) {
#ifdef FX_Z63_RUNTIME
        if(id>=(int32_t)TEXT+4 && id<=(int32_t)TEXT+6) {
            State *s=state();uint32_t row=(uint32_t)id-TEXT-4u;
            const char *label="Clear startup guard";
            if(!row)label=atomic_load(&s->startup.desired)&FX_STARTUP_AUTO?"Autostart: On":"Autostart: Off";
            if(row==2u) {
                static const char *const phases[]={"Startup: Checking","Startup: Ready","Startup: Guard present",
                    "Startup: I/O error","Startup: Armed","Startup: Running","Startup: Fault","Startup: Stopped"};
                uint32_t phase=atomic_load_explicit(&s->startup.phase,memory_order_acquire);
                label=phase<8u?phases[phase]:"Startup: Unknown";
            }
            uint32_t n=0u;while(label[n] && n<63u){s->boot_text[row][n]=(uint16_t)(uint8_t)label[n];++n;}
            s->boot_text[row][n]=0u;*text=s->boot_text[row];*bytes=n*2u;return;
        }
#endif
#ifdef FX_Z63_UI
        if(fx_z63_ui_text((uint32_t)id,text,bytes))return;
#endif
#ifdef FX_Z63_PTP
        if(id==(int32_t)TEXT+3){*text=debug;*bytes=sizeof(debug);return;}
#endif
        if(id==(int32_t)TEXT){*text=title;*bytes=sizeof(title);return;}
        if(id==(int32_t)TEXT+1){*text=run;*bytes=sizeof(run);return;}
        if(id==(int32_t)TEXT+2){*text=back;*bytes=sizeof(back);return;}
    }
    int32_t count=*(volatile int32_t *)(uintptr_t)UINT64_C(0xa02822854),index=count>id?id:0;
    const uint32_t *offsets=((const uint32_t *const *)(uintptr_t)(*(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f988)))[language];
    const uint8_t *pool=((const uint8_t *const *)(uintptr_t)(*(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f978)))[language];
    *text=(const uint16_t *)(const void *)(pool+(int32_t)offsets[index]);*bytes=offsets[index+1]-offsets[index];
}
extern uint64_t fx_z63_navigate_original(uint32_t);
uint64_t fx_z63_menu_navigate(uint32_t page) {
    if(page>=PAGE && page<0x9500u){
        if(!state())return 0u;
#ifdef FX_Z63_UI
        if(page!=PAGE && !fx_z63_ui_page(page))return 0u;
#else
        if(page!=PAGE)return 0u;
#endif
        return ((uint64_t (*)(uint32_t,uint32_t))(uintptr_t)0x403be714u)(2u,page);
    }
    return fx_z63_navigate_original(page);
}
