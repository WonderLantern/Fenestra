#include "fenestra_async.h"
#include "fenestra_native_hook.h"
#include "fenestra_time.h"
#include "fenestra_ui_list_sdk.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    FxServiceExchange exchange = {0};
    FxServiceModule api = {.service_abi = FX_SERVICE_ABI, .generation = 3u, .exchange = &exchange};
    FxServiceReply first = {0}, second = {0};
    FxSdkCommand commands[] = {{.operation = FX_S_LOG, .reply = &first},
                               {.operation = FX_S_LOG, .reply = &second}};
    FxSdkBatch batch = {.commands = commands, .count = 2u};
    assert(fx_sdk_batch_step(&batch, &api) == FX_BUSY);
    uint32_t submitted = exchange.request.sequence;
    fx_sdk_batch_cancel(&batch);
    assert(fx_sdk_batch_step(&batch, &api) == FX_BUSY);
    exchange.reply = (FxServiceReply){.sequence = submitted, .result = FX_OK, .value = {77u}};
    assert(fx_sdk_batch_step(&batch, &api) == FX_INTERRUPTED);
    assert(first.value[0] == 77u && !second.sequence && exchange.request.sequence == submitted);

    FxUiListItem item = {.id = 2u, .value = 123u};
    FxSdkClient client = {0};
    assert(fx_sdk_list_item(&client, &api, 1u, &item, "Choice", 1) == FX_OK);
    FxServiceReply published = {.result = FX_OK, .value = {7u, 90u}};
    assert(fx_sdk_list_published(&item, &published) == FX_OK);
    FxUiEvent event = {.control = 2u, .event = FX_INPUT_ACTIVATE, .serial = 91u, .reserved = 6u};
    assert(!fx_sdk_list_selected(&item, &event));
    event.reserved = 7u;
    assert(fx_sdk_list_selected(&item, &event) && item.value == 123u);
    event.serial = 90u;
    assert(!fx_sdk_list_selected(&item, &event));

    _Atomic uintptr_t slot = 11u;
    FxNativePointerHook hook = {0};
    assert(fx_native_hook_pointer(&hook, &slot, 10u, 22u) == FX_STALE);
    assert(fx_native_hook_pointer(&hook, &slot, 11u, 22u) == FX_OK);
    atomic_store(&slot, 33u);
    assert(fx_native_hook_restore(&hook) == FX_STALE && atomic_load(&slot) == 33u);
    atomic_store(&slot, 22u);
    assert(fx_native_hook_restore(&hook) == FX_OK && atomic_load(&slot) == 11u);

    uint64_t converted = 0u;
    assert(fx_time_scale(UINT64_MAX, 1000000000u, 1000u, &converted) == FX_OK);
    assert(converted == UINT64_MAX / 1000000u);
    assert(fx_time_scale(UINT64_MAX, 1u, 1000u, &converted) == FX_BOUNDS);
    puts("SDK helpers: pending cancellation, UI revision fences, hook conflict and large clocks passed");
    return 0;
}
