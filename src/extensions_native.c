#include "fenestra_extensions.h"
#include "fenestra_module_bus.h"
static uintptr_t function(FxPlatformOwner *o, const char *name) {
    const FxNamedBinding *b = fx_platform_find(o, name);
    return b && b->kind == FX_BIND_FUNCTION ? b->address : 0u;
}
static void *allocate(void *context, uint32_t bytes) {
    uintptr_t fn = function(context, "rtos.memory.allocate");
    return fn ? ((void *(*)(uint32_t, uint32_t))fn)(bytes, 0u) : 0;
}
static void release(void *context, void *memory) {
    uintptr_t fn = function(context, "rtos.memory.release");
    if (fn)
        ((void (*)(void *))fn)(memory);
}
FxExtensionDomain *fx_native_extensions(FxPlatformOwner *owner) {
    FxModuleBus *bus = fx_native_module_bus();
    if (!bus || !owner)
        return 0;
    uintptr_t domain = atomic_load_explicit(&bus->extensions, memory_order_acquire);
    if (domain)
        return (void *)domain;
    FxExtensionDomain *created =
        fx_extensions_create((FxIndexMemory){owner, allocate, release}, 4u * 1024u * 1024u);
    if (!created)
        return 0;
    uintptr_t zero = 0u;
    if (atomic_compare_exchange_strong_explicit(&bus->extensions, &zero, (uintptr_t)created,
                                                memory_order_release, memory_order_acquire))
        return created;
    release(owner, created);
    return (void *)zero;
}
