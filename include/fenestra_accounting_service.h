#ifndef FENESTRA_ACCOUNTING_SERVICE_H
#define FENESTRA_ACCOUNTING_SERVICE_H
#include "fenestra_platform.h"
/* Read-only owner snapshots, copied through core.resources/v1. The native
 * runtime owns this state; a module cannot submit counter updates/pointers. */
FxResult fx_account_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,
    uint64_t *,const FxServiceRequest *,FxServiceReply *);
/* Before the first binding allocation, after trusted owner initialization. */
void fx_account_owner_bind(FxPlatformOwner *);
FxAllocationObserver fx_account_bus_queues(FxPlatformOwner *);
FxAllocationObserver fx_account_task_storage(FxPlatformOwner *);
FxAllocationObserver fx_account_hook_storage(FxPlatformOwner *);
#endif
