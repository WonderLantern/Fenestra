/* Headless EL0 entry/return test; no camera capability or hardware operation. */
#include "fenestra_services.h"
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->abi!=FX_ABI || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(api->service_abi!=FX_SERVICE_ABI || !api->exchange)return 2;
    if(api->event!=FX_EVENT_INIT && api->event!=FX_EVENT_TICK && api->event!=FX_EVENT_STOP)return 3;
    api->exchange->request.sequence=api->event;
    api->exchange->request.arg[0]=api->event;
    return 0;
}
