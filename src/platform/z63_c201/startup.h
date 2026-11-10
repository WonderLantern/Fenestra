#ifndef FX_Z63_STARTUP_H
#define FX_Z63_STARTUP_H
#include "fenestra_startup_files.h"
#include "task.h"
enum {FX_Z63_BOOT_ACTIVATE=1u,FX_Z63_BOOT_AUTO,FX_Z63_BOOT_CLEAR};
typedef struct {
    FxStartup journal;FxStartupIO io;FxStartupFiles files;FxZ63TaskScope task;
    _Atomic uint32_t started,command,shutdown,cleanup_window,shutdown_done;
    _Atomic uint32_t phase,desired,error;
    void *context;
    FxResult (*activate)(void *);
    uint32_t (*runtime_state)(void *); /* 0 pending, 1 published, 2 fault, 3 quiesced */
    uint32_t automatic_attempted;
    uint8_t stack[32768] __attribute__((aligned(16)));
} FxZ63Startup;
void fx_z63_startup_init(FxZ63Startup *);
FxResult fx_z63_startup_start(FxZ63Startup *);
FxResult fx_z63_startup_request(FxZ63Startup *,uint32_t);
void fx_z63_startup_shutdown(FxZ63Startup *);
#endif
