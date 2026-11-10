#if !defined(FX_ZF_EXPERIMENTAL_SYSCTL_GATE) || FX_ZF_EXPERIMENTAL_SYSCTL_GATE != 1
#error "Zf SYSCTL gate is experimental and is not enabled by a production builder"
#endif

#include <stdint.h>
#include "fenestra_memory.h"
#include "lifecycle.h"
#if defined(FX_ZF_EXPERIMENTAL_MENU_NATIVE) && FX_ZF_EXPERIMENTAL_MENU_NATIVE == 1
#include "menu_model.h"
#endif
#if defined(FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP) && FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP == 1
#include "owner_publish.h"
#endif
#if defined(FX_ZF_EXPERIMENTAL_OWNER_WORKER) && FX_ZF_EXPERIMENTAL_OWNER_WORKER == 1
#if !defined(FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP) || FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP != 1
#error "Zf owner worker requires the checked owner bootstrap"
#endif
#endif

/* The eventual lifecycle owner must serialize or otherwise handle reentrant
 * callbacks and make repeated events safe. Event 8 alone is not proof of a
 * normal shutdown; the owner must establish the necessary context before
 * taking action. Callbacks receive only copied scalars: the original message
 * may no longer be alive when the dispatcher returns. This file is not linked
 * into a supported target. */
extern void fx_zf_before_bs_shutdown_event(uint32_t, uint32_t);
extern void fx_zf_after_startup_complete(uint32_t, uint32_t);
extern void fx_zf_after_card_mount(uint32_t, uint32_t);
extern uint64_t fx_zf_system_original(void *, uint32_t);

uint64_t fx_zf_system_dispatch(void *message, uint32_t sync)
{
    uint32_t event = UINT32_MAX;
    if (message)
        memcpy(&event, (const uint8_t *)message + 0x20u, sizeof(event));
    if (event == 8u) {
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
        fx_zf_startup_before_shutdown();
#endif
        fx_zf_before_bs_shutdown_event(event, sync);
    }
    else if (event == 24u)
        fx_zf_before_card_remove(event, sync);
    uint64_t result = fx_zf_system_original(message, sync);
    if (event == 20u) {
#if defined(FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP) && FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP == 1
        /* Experimental only. A failed bootstrap leaves accessors absent and
         * preserves the original dispatcher result. No successful start or
         * clean shutdown is inferred from allocation/publication alone. */
        FxResult initialized = fx_zf_owner_bootstrap();
#if defined(FX_ZF_EXPERIMENTAL_MENU_NATIVE) && FX_ZF_EXPERIMENTAL_MENU_NATIVE == 1
        if (initialized == FX_OK)
            (void)fx_zf_menu_install();
#endif
#if defined(FX_ZF_EXPERIMENTAL_OWNER_WORKER) && FX_ZF_EXPERIMENTAL_OWNER_WORKER == 1
        /* Explicit QEMU research policy, previously tested by a private
         * wrapper. This worker consumes lifecycle notices only. Its atomic
         * start claim prevents duplicate tasks on repeated startup events;
         * failures retain task/stack state and never alter Nikon's result. */
        if (initialized == FX_OK)
            (void)fx_zf_owner_start_native_task(27u, 1u, 100u);
#else
        (void)initialized;
#endif
#endif
        fx_zf_after_startup_complete(event, sync);
    } else if (event == 25u)
        fx_zf_after_card_mount(event, sync);
    return result;
}
