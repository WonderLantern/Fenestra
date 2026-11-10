/* Z8 port derived from upstream src/platform/z9_c532/services.h.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#ifndef FENESTRA_Z8_C311_FULL_SERVICES_H
#define FENESTRA_Z8_C311_FULL_SERVICES_H
#include "fenestra_platform.h"
#include "fenestra_runtime_accounts.h"
typedef struct { FxPlatformOwner platform;FxRuntimeAccounts accounting; } FxZ8Services;
FxResult fx_z8_services_init(FxZ8Services *,uint32_t owner,uint32_t generation,uint32_t capabilities);
FxResult fx_z8_service_call(void *,const FxServiceRequest *,FxServiceReply *);
FxResult fx_z8_service_stop(void *,uint32_t panic);
#endif
