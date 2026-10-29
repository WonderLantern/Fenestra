#ifndef FENESTRA_PANIC_H
#define FENESTRA_PANIC_H
#include "fenestra_scene.h"
#include "fenestra_services.h"
#define FX_PANIC_COMMANDS 768u
/* Owned core snapshot. No module pointer or callback is followed to render it.
 * module is a retained, core-owned discovery filename. */
typedef struct {
    const char *module;
    FxResult error;
    int32_t module_result;
    uint32_t cpu_faults,software_interrupts,interrupt_reason;
    uint64_t esr,elr,far;
    uint32_t shooting,request_sequence,return_valid;
    uint64_t request_args[4];
} FxPanicReport;
/* Call after the serialized service dispatcher has completed. Rejected or
 * merely pending requests must not be attributed as the cause of a Panic. */
void fx_panic_service_request(FxPanicReport *,const FxServiceOwner *);
FxResult fx_panic_scene(FxScene *,const FxPanicReport *);
#endif
