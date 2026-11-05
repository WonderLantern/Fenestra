#ifndef FENESTRA_ASYNC_H
#define FENESTRA_ASYNC_H
#include "fenestra_sdk.h"
/* A module-owned continuation, not a worker/RTOS task. Immutable command and
 * data storage stays alive across entry returns. Results are copied before
 * the exchange is reused. Native operations may block in their real context. */
typedef struct {
    uint32_t operation, bytes;
    uint64_t args[4];
    const void *data;
    FxServiceReply *reply;
} FxSdkCommand;
typedef struct {
    FxSdkClient client;
    const FxSdkCommand *commands;
    uint32_t count, cursor, cancelled;
    FxResult result;
} FxSdkBatch;
static inline FxResult fx_sdk_batch_step(FxSdkBatch *b, const FxServiceModule *api) {
    if (!b || !api || (b->count && !b->commands))
        return FX_ARGUMENT;
    if (b->client.pending) {
        FxServiceReply reply;
        FxResult r = fx_sdk_take(&b->client, api, &reply);
        if (r != FX_OK)
            return r;
        if (b->cursor >= b->count)
            return FX_STATE;
        if (b->commands[b->cursor].reply)
            *b->commands[b->cursor].reply = reply;
        if (reply.result != FX_OK) {
            b->result = (FxResult)reply.result;
            return b->result;
        }
        ++b->cursor;
    }
    if (b->cancelled)
        return FX_INTERRUPTED; /* Never pretends a submitted call did not run. */
    if (b->result != FX_OK)
        return b->result;
    if (b->cursor == b->count)
        return FX_OK;
    const FxSdkCommand *q = &b->commands[b->cursor];
    FxResult r = fx_sdk_submit(&b->client, api, q->operation, q->args, q->data, q->bytes);
    if (r != FX_OK)
        b->result = r;
    return r == FX_OK ? FX_BUSY : r;
}
static inline void fx_sdk_batch_cancel(FxSdkBatch *b) {
    if (b)
        b->cancelled = 1u;
}
#endif
