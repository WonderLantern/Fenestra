/* An independently compiled module: one native page and an action button. */
#include "fenestra_sdk.h"
static FxSdkClient client;
static uint32_t stage;
static uint32_t presses;

int fenestra_module_entry(const FxModuleServices *base) {
    if (!base || base->bytes != sizeof(FxServiceModule)) return 1;
    const FxServiceModule *api = (const FxServiceModule *)base;
    if (api->service_abi != FX_SERVICE_ABI) return 2;
    if (client.pending) {
        FxServiceReply reply;
        FxResult taken = fx_sdk_take(&client, api, &reply);
        if (taken == FX_BUSY) return 0;
        if (taken != FX_OK) return 3;
        if (reply.result == FX_BUSY) { /* Submit the same stage again. */ }
        else if (stage < 2u && reply.result == FX_OK) ++stage;
        else if (stage == 2u && reply.result == FX_OK) {
            if (reply.bytes == sizeof(FxUiEvent)) {
                FxUiEvent event;
                for (uint32_t i=0; i<sizeof(event); ++i) ((uint8_t *)&event)[i]=reply.data[i];
                if (event.control == 2u && event.event == FX_INPUT_ACTIVATE) ++presses;
            }
        } else if (reply.result != FX_EMPTY) return 4;
    }
    FxResult result;
    if (stage < 2u) {
        FxUiControl control = {.abi=1u, .bytes=sizeof(control), .id=stage+1u,
            .parent=stage ? 1u : 0u, .kind=stage ? FX_UI_ACTION : FX_UI_PAGE,
            .flags=FX_UI_VISIBLE|FX_UI_ENABLED};
        const char *label=stage ? "Press" : "Hello module";
        for (uint32_t i=0; label[i] && i<sizeof(control.label)-1u; ++i) control.label[i]=label[i];
        result=fx_sdk_ui(&client,api,&control);
    } else result=fx_sdk_events(&client,api);
    return result == FX_OK ? 0 : 5;
}
