/* One native task serializes card journal/config IO. No GUI callback performs
 * filesystem work. An uncertain or unfinished stop retains the boot guard. */
#include "startup.h"
#include "fenestra_memory.h"
#include "z63_startup_bindings.h"
#define FN(a,t) ((t)(uintptr_t)(a))
static const FxStartupFileProfile profile={
    FN(FX_Z63_STARTUP_OPEN,int32_t (*)(const char *,uint32_t,uint32_t)),
    FN(FX_Z63_STARTUP_READ,uint32_t (*)(int32_t,void *,uint32_t,uint32_t *)),
    FN(FX_Z63_STARTUP_WRITE,uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *)),
    FN(FX_Z63_STARTUP_CLOSE,uint32_t (*)(int32_t)),
    FN(FX_Z63_STARTUP_REMOVE,uint32_t (*)(const char *)),
    {"A:\\FENESTRA\\fx63boot.flg","A:\\FENESTRA\\fx63cfg.0","A:\\FENESTRA\\fx63cfg.1"},
    UINT32_C(0x80000010)
};
static void publish(FxZ63Startup *s) {
    atomic_store(&s->desired,s->journal.desired);
    atomic_store(&s->error,(uint32_t)s->journal.error);
    atomic_store_explicit(&s->phase,s->journal.phase,memory_order_release);
}
static void activate(FxZ63Startup *s) {
    if(s->journal.phase!=FX_STARTUP_READY || atomic_load(&s->shutdown))return;
    uint64_t counter;__asm__ volatile("mrs %0,cntpct_el0":"=r"(counter));
    FxResult r=fx_startup_arm(&s->journal,&s->io,counter?counter:1u);
    if(r==FX_OK && !atomic_load(&s->shutdown))r=s->activate(s->context);
    else if(r==FX_OK)r=FX_STATE;
    if(r!=FX_OK)fx_startup_fault(&s->journal,r);
}
static void worker(void *context) {
    FxZ63Startup *s=context;
    (void)fx_startup_inspect(&s->journal,&s->io);publish(s);
    for(;;) {
        if(atomic_load_explicit(&s->shutdown,memory_order_acquire)) {
            /* The SYSCTL owner limits this window. Do not remove a guard
             * merely because FINISH returned or the controller stopped. */
            if(!atomic_load(&s->shutdown_done) && atomic_load(&s->cleanup_window)) {
                if(s->runtime_state(s->context)==3u && s->journal.phase==FX_STARTUP_RUNNING) {
                    (void)fx_startup_clean_shutdown(&s->journal,&s->io,1);publish(s);
                    atomic_store_explicit(&s->shutdown_done,1u,memory_order_release);
                } else if(s->journal.phase!=FX_STARTUP_RUNNING && s->journal.phase!=FX_STARTUP_ARMED)
                    atomic_store_explicit(&s->shutdown_done,1u,memory_order_release);
            }
        } else {
            uint32_t command=atomic_exchange_explicit(&s->command,0u,memory_order_acquire);
            if(s->journal.phase==FX_STARTUP_IO_ERROR && command)
                (void)fx_startup_inspect(&s->journal,&s->io);
            if(s->journal.phase==FX_STARTUP_READY && !s->automatic_attempted) {
                s->automatic_attempted=1u;
                if(s->journal.desired&FX_STARTUP_AUTO)activate(s);
            }
            if(command==FX_Z63_BOOT_ACTIVATE)activate(s);
            else if(command==FX_Z63_BOOT_CLEAR)(void)fx_startup_clear_abnormal(&s->journal,&s->io);
            else if(command==FX_Z63_BOOT_AUTO)
                (void)fx_startup_configure(&s->journal,&s->io,s->journal.desired^FX_STARTUP_AUTO);
            if(s->journal.phase==FX_STARTUP_ARMED && s->runtime_state(s->context)==1u)
                fx_startup_running(&s->journal);
            if((s->journal.phase==FX_STARTUP_ARMED || s->journal.phase==FX_STARTUP_RUNNING) &&
               s->runtime_state(s->context)==2u)fx_startup_fault(&s->journal,FX_INTEGRITY);
            publish(s);
        }
        if(fx_z63_task_delay_ticks(10u)!=FX_OK)return;
    }
}
void fx_z63_startup_init(FxZ63Startup *s) {
    memset(s,0,sizeof(*s));fx_startup_init_profile(&s->journal,0,630201u);
    s->files=(FxStartupFiles){.profile=&profile,.shutdown=&s->shutdown,.cleanup_window=&s->cleanup_window};
    (void)fx_startup_files(&s->io,&s->files);publish(s);
}
FxResult fx_z63_startup_start(FxZ63Startup *s) {
    if(!s || !s->activate || !s->runtime_state)return FX_ARGUMENT;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong(&s->started,&zero,1u))return s->task.id>0?FX_OK:FX_BUSY;
    FxResult r=fx_z63_task_start(&s->task,worker,s,s->stack,sizeof(s->stack));
    if(r!=FX_OK){atomic_store(&s->error,r);atomic_store(&s->phase,FX_STARTUP_IO_ERROR);}
    return r;
}
FxResult fx_z63_startup_request(FxZ63Startup *s,uint32_t command) {
    if(!s || command<1u || command>3u || atomic_load(&s->shutdown))return FX_ARGUMENT;
    FxResult r=fx_z63_startup_start(s);if(r!=FX_OK)return r;
    uint32_t zero=0u;
    return atomic_compare_exchange_strong_explicit(&s->command,&zero,command,memory_order_release,memory_order_relaxed)?FX_OK:FX_BUSY;
}
void fx_z63_startup_shutdown(FxZ63Startup *s) {
    if(!s || !atomic_load(&s->started))return;
    atomic_store(&s->cleanup_window,1u);atomic_store_explicit(&s->shutdown,1u,memory_order_release);
    for(uint32_t i=0u;i<50u && !atomic_load_explicit(&s->shutdown_done,memory_order_acquire);++i)
        if(fx_z63_task_delay_ticks(10u)!=FX_OK)break;
    atomic_store(&s->cleanup_window,0u);
}
