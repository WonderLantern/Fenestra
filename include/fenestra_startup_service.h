#ifndef FENESTRA_STARTUP_SERVICE_H
#define FENESTRA_STARTUP_SERVICE_H
#include "fenestra_startup.h"
#include <stdatomic.h>
enum {FX_START_ACTIVATE=1,FX_START_AUTO_ON,FX_START_AUTO_OFF,FX_START_CLEAR};
typedef struct {
    void *context;
    FxResult (*activate)(void *);
    /* OK = all started modules ready, BUSY = still loading, other = fault. */
    FxResult (*health)(void *);
    /* Request native resource stop and return OK only once every owner ACKs. */
    FxResult (*stop)(void *);
    uint64_t (*token)(void *);
} FxStartupBackend;
typedef struct {
    FxStartup journal;FxStartupIO io;FxStartupBackend backend;
    _Atomic uint32_t command,shutdown,cleanup_window,shutdown_done,inspect_requested;
    _Atomic uint32_t phase,desired,error,flag_known;
    uint32_t inspect_attempts,automatic_attempted;
} FxStartupService;
void fx_startup_service_init(FxStartupService *,uint32_t identity,FxStartupBackend);
/* A single owner calls tick. UI/system tasks only submit atomic requests. */
void fx_startup_service_tick(FxStartupService *);
FxResult fx_startup_service_request(FxStartupService *,uint32_t);
#endif
