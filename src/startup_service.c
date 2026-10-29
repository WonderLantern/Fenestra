#include "fenestra_startup_service.h"
#include "fenestra_memory.h"
void fx_startup_service_init(FxStartupService *s,uint32_t identity,FxStartupBackend backend) {
    memset(s,0,sizeof(*s));fx_startup_init_profile(&s->journal,0,identity);
    s->backend=backend;atomic_store(&s->desired,s->journal.desired);
}
static void activate(FxStartupService *s) {
    if(s->journal.phase!=FX_STARTUP_READY || atomic_load(&s->shutdown))return;
    uint64_t token=s->backend.token(s->backend.context);
    FxResult r=fx_startup_arm(&s->journal,&s->io,token?token:1u);
    if(r==FX_OK)r=atomic_load(&s->shutdown)?FX_STATE:s->backend.activate(s->backend.context);
    if(r!=FX_OK)fx_startup_fault(&s->journal,r);
}
void fx_startup_service_tick(FxStartupService *s) {
    if(atomic_load_explicit(&s->shutdown,memory_order_acquire)) {
        if(!atomic_load(&s->shutdown_done)) {
            FxResult stopped=s->backend.stop(s->backend.context);
            /* A shutdown racing activation must not bless incomplete startup. */
            if(s->journal.phase==FX_STARTUP_ARMED)fx_startup_fault(&s->journal,FX_STATE);
            if(stopped!=FX_OK && stopped!=FX_BUSY)fx_startup_fault(&s->journal,stopped);
            if(stopped==FX_OK) {
                if(atomic_load(&s->cleanup_window) && s->journal.phase==FX_STARTUP_RUNNING)
                    (void)fx_startup_clean_shutdown(&s->journal,&s->io,1);
                atomic_store_explicit(&s->shutdown_done,1u,memory_order_release);
            }
        }
    } else {
        uint32_t command=atomic_exchange_explicit(&s->command,0u,memory_order_acquire);
        uint32_t inspect=atomic_exchange_explicit(&s->inspect_requested,0u,memory_order_acquire);
        if((s->journal.phase==FX_STARTUP_UNCHECKED || s->journal.phase==FX_STARTUP_IO_ERROR) &&
           (s->inspect_attempts<100u || inspect || command==FX_START_ACTIVATE)) {
            ++s->inspect_attempts;
            (void)fx_startup_inspect(&s->journal,&s->io);
        }
        /* Auto is attempted exactly once after a positively clean inspection.
         * A user setting On is persisted for the next boot, not run now. */
        if(!s->automatic_attempted && s->journal.phase!=FX_STARTUP_UNCHECKED &&
           s->journal.phase!=FX_STARTUP_IO_ERROR) {
            s->automatic_attempted=1u;
            if(s->journal.phase==FX_STARTUP_READY && (s->journal.desired&FX_STARTUP_AUTO))activate(s);
        }
        if(command==FX_START_ACTIVATE)activate(s);
        if(command==FX_START_CLEAR)(void)fx_startup_clear_abnormal(&s->journal,&s->io);
        if(command==FX_START_AUTO_ON || command==FX_START_AUTO_OFF) {
            uint32_t desired=s->journal.desired&~FX_STARTUP_AUTO;
            if(command==FX_START_AUTO_ON)desired|=FX_STARTUP_AUTO;
            (void)fx_startup_configure(&s->journal,&s->io,desired);
        }
        if(s->journal.phase==FX_STARTUP_ARMED || s->journal.phase==FX_STARTUP_RUNNING) {
            FxResult r=s->backend.health(s->backend.context);
            if(r==FX_OK)fx_startup_running(&s->journal);
            else if(r!=FX_BUSY)fx_startup_fault(&s->journal,r);
        }
    }
    atomic_store(&s->desired,s->journal.desired);
    atomic_store(&s->error,(uint32_t)s->journal.error);
    atomic_store(&s->flag_known,s->journal.flag_known);
    atomic_store_explicit(&s->phase,s->journal.phase,memory_order_release);
}
FxResult fx_startup_service_request(FxStartupService *s,uint32_t command) {
    if(!s || command<FX_START_ACTIVATE || command>FX_START_CLEAR || atomic_load(&s->shutdown))return FX_STATE;
    uint32_t empty=0;
    return atomic_compare_exchange_strong(&s->command,&empty,command)?FX_OK:FX_BUSY;
}
