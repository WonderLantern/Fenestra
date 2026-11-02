#ifndef FENESTRA_ZF_C301_SERVICES_H
#define FENESTRA_ZF_C301_SERVICES_H

#include "fenestra_platform.h"
#include "loaded_modules.h"
#include "fenestra_worker_wait.h"
#include "fenestra_worker_pace.h"
#include "fenestra_runtime_accounts.h"

/* Zf keeps the same pointer-free module ABI as the other targets.  The
 * platform owner is the trusted backend; it is never passed as the module's
 * API prefix. */
typedef struct {
    FxPlatformOwner platform;
    FxRuntimeAccounts accounting;
    FxZfLoadedModule loaded;
    FxServiceOwner protocol;
    FxServiceExchange exchange;
    FxServiceModule module;
    uint32_t dispatched_sequence;
    FxWorkerWait wait;
    FxWorkerPace pace;
    _Atomic uint32_t action;
    uint32_t pacing;
    uint64_t next_due,tick_due;
} FxZfServices;

FxResult fx_zf_services_init(FxZfServices *, uint32_t owner, uint32_t generation, uint32_t capabilities);
/* Original per-module worker owns its own copied-message protocol and wait. */
FxResult fx_zf_services_init_worker(FxZfServices *,uint32_t,uint32_t,uint32_t);
FxResult fx_zf_service_call(void *, const FxServiceRequest *, FxServiceReply *);
FxResult fx_zf_service_stop(void *, uint32_t panic);
FxResult fx_zf_services_stop(FxZfServices *,uint32_t panic);
FxResult fx_zf_services_prepare(FxZfServices *, uint64_t now,uint32_t event,uint32_t value);
FxResult fx_zf_services_dispatch_pending(FxZfServices *);
/* Trusted loader only, after package digest/identity and image preparation.
 * Success retains service/image/record; does not invoke module INIT. */
FxResult fx_zf_services_bind_module(FxZfServices *,const char *,const FxModule *,void *,uint32_t);

#endif
