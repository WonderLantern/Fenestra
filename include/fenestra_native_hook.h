#ifndef FENESTRA_NATIVE_HOOK_H
#define FENESTRA_NATIVE_HOOK_H
#include "fenestra.h"
#include <stdatomic.h>
/* Optional pointer-hook helper. No global slot pool or policy VM. Code and
 * context stay resident after restore because stock callers may cache them.
 * The module owns the actual callback ABI, chaining and all asynchronous work. */
typedef struct {
    _Atomic uintptr_t *slot;
    uintptr_t original, replacement;
    uint32_t installed;
} FxNativePointerHook;
static inline FxResult fx_native_hook_pointer(FxNativePointerHook *hook, _Atomic uintptr_t *slot,
                                              uintptr_t expected, uintptr_t replacement) {
    if (!hook || !slot || ((uintptr_t)slot & 7u) || !replacement || hook->installed)
        return FX_ARGUMENT;
    if (!atomic_compare_exchange_strong_explicit(slot, &expected, replacement, memory_order_acq_rel,
                                                 memory_order_acquire))
        return FX_STALE;
    *hook = (FxNativePointerHook){slot, expected, replacement, 1u};
    return FX_OK;
}
static inline FxResult fx_native_hook_restore(FxNativePointerHook *hook) {
    if (!hook || !hook->installed)
        return FX_STATE;
    uintptr_t expected = hook->replacement;
    if (!atomic_compare_exchange_strong_explicit(hook->slot, &expected, hook->original, memory_order_acq_rel,
                                                 memory_order_acquire))
        return FX_STALE;
    hook->installed = 0u;
    return FX_OK;
}
#endif
