#ifndef FENESTRA_C110_STARTUP_H
#define FENESTRA_C110_STARTUP_H
#include "fenestra_startup.h"
#include "fenestra_startup_files.h"
#include <stdatomic.h>
enum { FX_BOOT_ACTIVATE=1,FX_BOOT_AUTO_TOGGLE,FX_BOOT_DEMO_TOGGLE,FX_BOOT_CLEAR,FX_BOOT_INSPECT };
typedef struct {
    FxStartup journal;
    FxStartupIO io;
    FxStartupFiles files;
    _Atomic uint32_t phase,desired,activation,error,command,shutdown,shutdown_done;
    _Atomic uint32_t cleanup_window;
    _Atomic uint32_t inspect_requested;
    _Atomic uint32_t started;
    uint32_t automatic_attempted;
    _Atomic int32_t task;
    void *context;
    FxResult (*activate)(void *,uint32_t);
    uint32_t (*core_phase)(void *);
    int (*stop)(void *);
    void (*refresh)(void *);
    uint8_t stack[16384] __attribute__((aligned(16)));
} FxC110Startup;
void fx_c110_startup_init(FxC110Startup *);
FxResult fx_c110_startup_start(FxC110Startup *);
FxResult fx_c110_startup_request(FxC110Startup *,uint32_t);
void fx_c110_startup_shutdown(FxC110Startup *);
void fx_c110_startup_files(FxC110Startup *);
void fx_c110_menu_system_event(uint32_t,int);
#endif
