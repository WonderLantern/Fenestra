#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fenestra.h"

uint64_t fx_zf_system_dispatch(void *, uint32_t);
static char calls[8];
static unsigned count;
static void *observed_message;
static uint32_t observed_sync;
static uint32_t callback_event, callback_sync;
static int rewrite_event, release_message;
static FxResult bootstrap_result = FX_OK;
static unsigned worker_calls;
static FxResult worker_result = FX_OK;
#if defined(FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP) && FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP == 1
#if defined(FX_ZF_EXPERIMENTAL_OWNER_WORKER) && FX_ZF_EXPERIMENTAL_OWNER_WORKER == 1
#if defined(FX_ZF_EXPERIMENTAL_MENU_NATIVE) && FX_ZF_EXPERIMENTAL_MENU_NATIVE == 1
#define STARTUP_CALLS "OBMWA"
#else
#define STARTUP_CALLS "OBWA"
#endif
#else
#define STARTUP_CALLS "OBA"
#endif
FxResult fx_zf_owner_bootstrap(void) { calls[count++]='B';return bootstrap_result; }
FxResult fx_zf_menu_install(void) {calls[count++]='M';return FX_IO;}
#else
#define STARTUP_CALLS "OA"
#endif
FxResult fx_zf_owner_start_native_task(uint32_t priority,uint32_t mode,uint32_t ticks) {
    assert(priority==27u && mode==1u && ticks==100u);
    calls[count++]='W'; ++worker_calls; return worker_result;
}

void fx_zf_before_bs_shutdown_event(uint32_t event,uint32_t sync) { callback_event=event;callback_sync=sync;calls[count++] = 'S'; }
void fx_zf_after_startup_complete(uint32_t event,uint32_t sync) { callback_event=event;callback_sync=sync;calls[count++] = 'A'; }
void fx_zf_after_card_mount(uint32_t event,uint32_t sync) { callback_event=event;callback_sync=sync;calls[count++] = 'C'; }
void fx_zf_before_card_remove(uint32_t event,uint32_t sync) { callback_event=event;callback_sync=sync;calls[count++] = 'R'; }
uint64_t fx_zf_system_original(void *message, uint32_t sync)
{
    calls[count++] = 'O';
    observed_message = message;
    observed_sync = sync;
    if (message && rewrite_event) {
        uint32_t changed = 25;
        memcpy((uint8_t *)message + 32, &changed, sizeof(changed));
    }
    if (message && release_message) free(message);
    return UINT64_C(0xfedcba9876543210);
}

static void check(uint32_t event, uint32_t sync, const char *expected)
{
    uint8_t message[56] = {0}, before[56];
    memcpy(message + 32, &event, sizeof(event));
    memcpy(before, message, sizeof(message));
    memset(calls, 0, sizeof(calls));
    count = 0;
    assert(fx_zf_system_dispatch(message, sync) == UINT64_C(0xfedcba9876543210));
    assert(observed_message == message && observed_sync == sync);
    assert(!strcmp(calls, expected));
    if (strlen(expected) > 1) assert(callback_event == event && callback_sync == sync);
    assert(!memcmp(message, before, sizeof(message)));
}

int main(void)
{
    check(8, 0, "SO");
    check(20, 0, STARTUP_CALLS);
    check(25, 0, "OC");
    check(7, 0, "O");
    check(19, 1, "O");
    check(24, 0, "RO");
    check(UINT32_MAX, 1, "O");
    count = 0;
    memset(calls, 0, sizeof(calls));
    assert(fx_zf_system_dispatch(0, 1) == UINT64_C(0xfedcba9876543210));
    assert(observed_message == 0 && observed_sync == 1 && !strcmp(calls, "O"));

    uint8_t *dynamic = calloc(1,56);
    assert(dynamic);
    uint32_t event = 20;
    memcpy(dynamic + 32, &event, sizeof(event));
    count = 0; memset(calls, 0, sizeof(calls)); rewrite_event = 1;
    assert(fx_zf_system_dispatch(dynamic, 1) == UINT64_C(0xfedcba9876543210));
    assert(!strcmp(calls, STARTUP_CALLS) && callback_event == 20 && callback_sync == 1);
    rewrite_event = 0; free(dynamic);

    dynamic = calloc(1,56);
    assert(dynamic);
    event = 25; memcpy(dynamic + 32, &event, sizeof(event));
    count = 0; memset(calls, 0, sizeof(calls)); release_message = 1;
    assert(fx_zf_system_dispatch(dynamic, 0) == UINT64_C(0xfedcba9876543210));
    assert(!strcmp(calls, "OC") && callback_event == 25 && callback_sync == 0);
    release_message = 0;
#if defined(FX_ZF_EXPERIMENTAL_OWNER_WORKER) && FX_ZF_EXPERIMENTAL_OWNER_WORKER == 1
    unsigned before_workers = worker_calls;
    bootstrap_result = FX_STATE;
    check(20, 0, "OBA");
    assert(worker_calls == before_workers);
    bootstrap_result = FX_OK;
    worker_result = FX_IO;
    check(20, 0, STARTUP_CALLS);
    assert(worker_calls == before_workers + 1u);
#else
    (void)bootstrap_result;
#endif
    return 0;
}
