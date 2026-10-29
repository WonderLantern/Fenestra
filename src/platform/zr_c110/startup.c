#include "fenestra_c110_startup.h"
#include "fenestra_memory.h"
#include "fenestra_ptp.h"
#include "fenestra_c110_rtos.h"
#define FN(a,t) ((t)(uintptr_t)(a))
typedef struct {
    uint32_t attributes,reserved;uintptr_t context,entry;
    uint32_t priority,reserved2;uint64_t stack_bytes;uintptr_t stack;
    uint32_t processor,reserved3;
} Task;
_Static_assert(sizeof(Task)==56,"C1.10 task record");
static void publish(FxC110Startup *s) {
    atomic_store(&s->desired,s->journal.desired);
    atomic_store(&s->activation,s->journal.activation);
    atomic_store(&s->error,(uint32_t)s->journal.error);
    atomic_store_explicit(&s->phase,s->journal.phase,memory_order_release);
    s->refresh(s->context);
}
static void activate(FxC110Startup *s) {
    if(s->journal.phase==FX_STARTUP_UNCHECKED || s->journal.phase==FX_STARTUP_IO_ERROR)
        (void)fx_startup_inspect(&s->journal,&s->io);
    if(atomic_load(&s->shutdown))return;
    if(s->journal.phase==FX_STARTUP_ARMED && s->core_phase(s->context)==9u) {
        FxResult retry=s->activate(s->context,s->journal.activation);
        if(retry!=FX_OK)fx_startup_fault(&s->journal,retry);
        return;
    }
    if(s->journal.phase!=FX_STARTUP_READY)return;
    uint64_t counter;__asm__ volatile("mrs %0,cntpct_el0":"=r"(counter));
    FxResult r=fx_startup_arm(&s->journal,&s->io,counter?counter:1u);
    if(r==FX_OK && !atomic_load(&s->shutdown))r=s->activate(s->context,s->journal.activation);
    else if(r==FX_OK)r=FX_STATE;
    if(r!=FX_OK)fx_startup_fault(&s->journal,r);
}
static void worker(void *argument) {
    FxC110Startup *s=argument;
    /* Readiness notifications can precede card mounting. A bounded read-only
     * retry window covers normal startup; no repeated module activation. */
    for(uint32_t attempt=0;attempt<100u && !atomic_load(&s->shutdown);++attempt) {
        FxResult r=fx_startup_inspect(&s->journal,&s->io);publish(s);
        if(r==FX_OK || s->journal.phase==FX_STARTUP_ABNORMAL || r!=FX_IO)break;
        (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(100u);
    }
    if(!atomic_load(&s->shutdown) && s->journal.phase==FX_STARTUP_READY &&
       (s->journal.desired&FX_STARTUP_AUTO)) {s->automatic_attempted=1u;activate(s);publish(s);}
    for(;;) {
        /* Separate native owner observes RTOS debug jobs even if the core
         * worker is busy. This still depends on the Nikon scheduler running;
         * it is not advertised as a hardware watchdog. */
        FxPtp *ptp=fx_c110_menu_ptp();
        if(ptp)fx_c110_rtos_tick(atomic_load(&ptp->backend_session),atomic_load(&ptp->generation),
            fx_c110_menu_ptp_active() && atomic_load(&ptp->enabled));
        if(atomic_load_explicit(&s->shutdown,memory_order_acquire) && !atomic_load(&s->shutdown_done)) {
            if(s->journal.phase==FX_STARTUP_ARMED) {
                uint32_t core=s->core_phase(s->context);
                if(core>=2u && core<=4u)fx_startup_running(&s->journal);
            }
            int stopped=s->stop(s->context);
            if(stopped && atomic_load(&s->cleanup_window) && s->journal.phase==FX_STARTUP_RUNNING)
                (void)fx_startup_clean_shutdown(&s->journal,&s->io,1);
            if(stopped || s->journal.phase==FX_STARTUP_FAULT || s->journal.phase==FX_STARTUP_ABNORMAL ||
               s->journal.phase==FX_STARTUP_READY || s->journal.phase==FX_STARTUP_IO_ERROR) {
                publish(s);atomic_store_explicit(&s->shutdown_done,1u,memory_order_release);
            }
        } else if(!atomic_load(&s->shutdown)) {
            uint32_t command=atomic_exchange_explicit(&s->command,0u,memory_order_acquire);
            uint32_t inspect=atomic_exchange_explicit(&s->inspect_requested,0u,memory_order_acquire);
            if((command==FX_BOOT_INSPECT || inspect) && (s->journal.phase==FX_STARTUP_UNCHECKED || s->journal.phase==FX_STARTUP_IO_ERROR)) {
                (void)fx_startup_inspect(&s->journal,&s->io);
                if(s->journal.phase==FX_STARTUP_READY && (s->journal.desired&FX_STARTUP_AUTO) && !s->automatic_attempted) {
                    s->automatic_attempted=1u;activate(s);
                }
            }
            if(command==FX_BOOT_ACTIVATE)activate(s);
            if(command==FX_BOOT_CLEAR)(void)fx_startup_clear_abnormal(&s->journal,&s->io);
            if(command==FX_BOOT_AUTO_TOGGLE || command==FX_BOOT_DEMO_TOGGLE)
                (void)fx_startup_configure(&s->journal,&s->io,s->journal.desired^
                    (command==FX_BOOT_AUTO_TOGGLE?FX_STARTUP_AUTO:FX_STARTUP_DEMO));
            if(s->journal.phase==FX_STARTUP_ARMED) {
                uint32_t phase=s->core_phase(s->context);
                if(phase>=2u && phase<=4u)fx_startup_running(&s->journal);
                else if(phase==5u || phase==6u)fx_startup_fault(&s->journal,FX_INTEGRITY);
            } else if(s->journal.phase==FX_STARTUP_RUNNING && s->core_phase(s->context)==5u)
                fx_startup_fault(&s->journal,FX_INTEGRITY);
            if(command || atomic_load(&s->phase)!=s->journal.phase)publish(s);
        }
        (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(33u);
    }
}
void fx_c110_startup_init(FxC110Startup *s) {
    memset(s,0,sizeof(*s));fx_startup_init(&s->journal,0);fx_c110_startup_files(s);
    atomic_store(&s->desired,FX_STARTUP_DEMO);
}
FxResult fx_c110_startup_start(FxC110Startup *s) {
    uint32_t empty=0;
    if(!atomic_compare_exchange_strong(&s->started,&empty,1u))return s->task>0?FX_OK:FX_BUSY;
    Task task={0,0,(uintptr_t)s,(uintptr_t)worker,28,0,sizeof(s->stack),(uintptr_t)s->stack,1,UINT32_MAX};
    s->task=FN(UINT64_C(0x4018507c),int32_t (*)(const Task *))(&task);
    if(s->task<=0 || FN(UINT64_C(0x401855f8),int32_t (*)(int32_t))(s->task)<0) {
        s->task=-1;atomic_store(&s->phase,FX_STARTUP_IO_ERROR);atomic_store(&s->error,FX_CAPABILITY);return FX_CAPABILITY;
    }
    return FX_OK;
}
FxResult fx_c110_startup_request(FxC110Startup *s,uint32_t command) {
    if(!s || s->task<=0 || atomic_load(&s->shutdown) || command<1u || command>5u)return FX_STATE;
    /* Card-ready notification must not be lost behind a pending UI command. */
    if(command==FX_BOOT_INSPECT){atomic_store_explicit(&s->inspect_requested,1u,memory_order_release);return FX_OK;}
    uint32_t empty=0;
    return atomic_compare_exchange_strong(&s->command,&empty,command)?FX_OK:FX_BUSY;
}
void fx_c110_startup_shutdown(FxC110Startup *s) {
    if(!s || !s->started || s->task<=0)return;
    atomic_store(&s->cleanup_window,1u);
    atomic_store_explicit(&s->shutdown,1u,memory_order_release);
    /* Do not delay Nikon shutdown indefinitely if a module/native call hangs.
     * Missing acknowledgement leaves boot.flag intact for the next boot. */
    for(uint32_t i=0;i<15u && !atomic_load_explicit(&s->shutdown_done,memory_order_acquire);++i)
        (void)FN(UINT64_C(0x40187b18),int32_t (*)(uint32_t))(33u);
    atomic_store(&s->cleanup_window,0u);
}
extern uint64_t fx_c110_system_original(void *,uint32_t);
uint64_t fx_c110_system_dispatch(void *event,uint32_t synchronous) {
    uint32_t id=*(const uint32_t *)((const uint8_t *)event+32u);
    if(id==11u)fx_c110_menu_system_event(id,1);
    uint64_t result=fx_c110_system_original(event,synchronous);
    if(id==20u || id==25u)fx_c110_menu_system_event(id,0);
    return result;
}
