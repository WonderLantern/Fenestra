#ifndef FENESTRA_Z63_C201_SERVICES_H
#define FENESTRA_Z63_C201_SERVICES_H
#include "fenestra_platform.h"
#include "fenestra_runtime_accounts.h"
typedef struct { FxPlatformOwner platform;FxRuntimeAccounts accounting; } FxZ63Services;
FxResult fx_z63_services_init(FxZ63Services *,uint32_t owner,uint32_t generation,uint32_t capabilities);
FxResult fx_z63_service_call(void *,const FxServiceRequest *,FxServiceReply *);
FxResult fx_z63_service_stop(void *,uint32_t panic);
#endif
