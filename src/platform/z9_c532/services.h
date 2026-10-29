#ifndef FENESTRA_Z9_C532_SERVICES_H
#define FENESTRA_Z9_C532_SERVICES_H
#include "fenestra_platform.h"
#include "fenestra_runtime_accounts.h"
typedef struct { FxPlatformOwner platform;FxRuntimeAccounts accounting; } FxZ9Services;
FxResult fx_z9_services_init(FxZ9Services *,uint32_t owner,uint32_t generation,uint32_t capabilities);
FxResult fx_z9_service_call(void *,const FxServiceRequest *,FxServiceReply *);
FxResult fx_z9_service_stop(void *,uint32_t panic);
#endif
