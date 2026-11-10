/* The payload forces a real package beyond the old 64 * 511-byte limit. */
#include "fenestra_api.h"
#include "fenestra_services.h"
static const unsigned char payload[40000]={[0]=17,[39999]=91};
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    volatile const unsigned char *bytes=payload;
    if(bytes[0]!=17 || bytes[39999]!=91 || bytes[20000])return 2;
    api->exchange->request.sequence=api->event;
    return 0;
}
