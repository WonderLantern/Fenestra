#ifndef FENESTRA_UI_LIST_SDK_H
#define FENESTRA_UI_LIST_SDK_H
#include "fenestra_sdk.h"
/* A large choice is an ordinary native PAGE with ACTION children. Core native
 * pagination handles its size. Values/identities remain module-owned; there
 * is no eight-option wire array and no core feature-specific picker. */
typedef struct {
    uint32_t id, revision;
    uint64_t serial, value;
} FxUiListItem;
static inline FxResult fx_sdk_list_item(FxSdkClient *c, const FxServiceModule *a, uint32_t parent,
                                        const FxUiListItem *item, const char *label, int selected) {
    if (!item || !item->id || !parent || !label)
        return FX_ARGUMENT;
    FxUiControl row = {.abi = 1u,
                       .bytes = sizeof(row),
                       .id = item->id,
                       .parent = parent,
                       .kind = FX_UI_ACTION,
                       .flags = FX_UI_VISIBLE | FX_UI_ENABLED};
    uint32_t n = 0u;
    while (n + 1u < sizeof(row.label) && label[n]) {
        row.label[n] = label[n];
        ++n;
    }
    /* Keep a truncated label valid UTF-8; the module may attach a full document. */
    if (label[n])
        while (n && ((uint8_t)label[n] & 0xc0u) == 0x80u)
            --n;
    row.label[n] = 0;
    if (selected) {
        row.text[0] = 'S';
        row.text[1] = 'e';
        row.text[2] = 'l';
        row.text[3] = 'e';
        row.text[4] = 'c';
        row.text[5] = 't';
        row.text[6] = 'e';
        row.text[7] = 'd';
    }
    return fx_sdk_ui(c, a, &row);
}
static inline FxResult fx_sdk_list_published(FxUiListItem *item, const FxServiceReply *reply) {
    if (!item || !reply)
        return FX_ARGUMENT;
    if (reply->result != FX_OK)
        return (FxResult)reply->result;
    item->revision = (uint32_t)reply->value[0];
    item->serial = reply->value[1];
    return FX_OK;
}
static inline int fx_sdk_list_selected(const FxUiListItem *item, const FxUiEvent *event) {
    return item && event && event->control == item->id && event->event == FX_INPUT_ACTIVATE &&
           event->serial > item->serial && (!event->reserved || event->reserved == item->revision);
}
#endif
