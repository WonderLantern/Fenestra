#include "fenestra_machine.h"
#include "fenestra_memory.h"
#include "fenestra_mmu_context.h"
#include "fenestra_native_mapping.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_resident.h"
static uintptr_t function(const FxPlatform *p, const char *name) {
    for (uint32_t i = 0u; i < p->count; ++i) {
        const FxNamedBinding *b = &p->bindings[i];
        uint32_t n = 0u;
        while (name[n] && b->name[n] == name[n])
            ++n;
        if (!name[n] && !b->name[n] && b->kind == FX_BIND_FUNCTION && b->abi == FX_CALL_U64_8)
            return b->address;
    }
    return 0u;
}
static void *allocate(void *context, uint32_t bytes) {
    uintptr_t fn = function(context, "rtos.memory.allocate");
    return fn ? ((void *(*)(uint32_t, uint32_t))fn)(bytes, 0u) : 0;
}
static void *source_allocate(void *context, uint32_t bytes) {
    uintptr_t fn = function(context, "rtos.memory.allocate");
    return fn ? ((void *(*)(uint32_t, uint32_t))fn)(bytes, 1u) : 0;
}
static void release(void *context, void *memory) {
    uintptr_t fn = function(context, "rtos.memory.release");
    if (fn)
        ((void (*)(void *))fn)(memory);
}
FxResidentHost fx_native_resident_host(const FxPlatform *p) {
    return (FxResidentHost){.memory = {(void *)p, allocate, release},
                            .source_memory = {(void *)p, source_allocate, release},
                            .refresh_ui = fx_native_ui_refresh_owner,
                            .protect = fx_native_resident_protect};
}
FxResult fx_native_resident_protect(void *context, FxPlatformOwner *o, uint8_t *memory, uint32_t bytes,
                                    uint32_t flags) {
    (void)context;
    if (!o || !memory || ((uintptr_t)memory & 4095u) || !bytes || (bytes & 4095u) ||
        (uintptr_t)memory > UINTPTR_MAX - bytes || !(flags & FX_READ) || (flags & ~7u) ||
        (flags & (FX_WRITE | FX_EXECUTE)) == (FX_WRITE | FX_EXECUTE))
        return FX_ARGUMENT;
    const FxNamedBinding *binding = fx_platform_find(o, "rtos.mmu.context");
    uintptr_t protect = function(o->platform, "rtos.mmu.context.protect"),
              attributes = function(o->platform, "rtos.mmu.context.attributes"),
              translate = function(o->platform, "rtos.mmu.context.translate"),
              invalidate = function(o->platform, "rtos.mmu.invalidate-all");
    FxNativeMappingRegistry *registry = fx_native_mappings();
    if (!binding || binding->kind != FX_BIND_TABLE || binding->bytes < sizeof(FxNativeMmuContext) ||
        !protect || !attributes || !translate || !invalidate || !registry || !o->platform->io.cache)
        return FX_CAPABILITY;
    uintptr_t delay = function(o->platform, "rtos.task.delay");
    if (!delay)
        return FX_CAPABILITY;
    for (;;) {
        uint32_t zero = 0u;
        if (atomic_compare_exchange_strong_explicit(&registry->lock, &zero, 1u, memory_order_acquire,
                                                    memory_order_relaxed))
            break;
        /* Loader task only, no owner/GUI/stock lock held. Yield to the writer
         * instead of turning benign publication contention into Panic. */
        if (((int32_t (*)(uint32_t))delay)(1u))
            return FX_IO;
    }

    FxNativeMmuContext *source = (void *)(uintptr_t)binding->address;
    uintptr_t roots[4] = {source->root, 0u, 0u, 0u};
    uint32_t count = 1u;
    if (source->private_roots)
        for (uint32_t i = 0u; i < 3u; ++i) {
            uint32_t seen = 0u;
            for (uint32_t j = 0u; j < count; ++j)
                if (roots[j] == source->roots[i])
                    seen = 1u;
            if (!seen)
                roots[count++] = source->roots[i];
        }
    FxResult result = FX_OK;
    for (uint32_t at = 0u; at < bytes && result == FX_OK; at += 4096u) {
        uint64_t first_pa = 0u;
        for (uint32_t i = 0u; i < count; ++i) {
            if (!roots[i] || (roots[i] & 4095u)) {
                result = FX_STATE;
                break;
            }
            FxNativeMmuContext view = *source;
            view.root = roots[i];
            view.private_roots = 0u;
            view.pages = fx_native_mmu_view_pages(source);
            uint64_t pa = 0u;
            uint32_t old = 0u;
            if (((int32_t (*)(void *, uint64_t, uint64_t *, int32_t))translate)(&view, (uintptr_t)memory + at,
                                                                                &pa, -1) ||
                ((int32_t (*)(void *, uint64_t, uint32_t *, int32_t))attributes)(
                    &view, (uintptr_t)memory + at, &old, -1) ||
                (i && pa != first_pa)) {
                result = FX_INTEGRITY;
                break;
            }
            first_pa = pa;
            uint32_t wanted = (old & ~3u) | ((flags & FX_WRITE) ? 0u : 1u) | ((flags & FX_EXECUTE) ? 2u : 0u);
            int32_t status = ((int32_t (*)(void *, uint64_t, uint64_t, uint32_t, uint32_t, int32_t))protect)(
                &view, (uintptr_t)memory + at, 4096u, wanted, 0u, -1);
            memcpy(source->allocated, view.allocated, sizeof(source->allocated));
            uint32_t actual = 0u;
            if (status ||
                ((int32_t (*)(void *, uint64_t, uint32_t *, int32_t))attributes)(
                    &view, (uintptr_t)memory + at, &actual, -1) ||
                actual != wanted) {
                result = FX_IO;
                break;
            }
        }
    }
    ((void (*)(void))invalidate)();
    if (result == FX_OK && (flags & FX_EXECUTE))
        for (uint32_t at = 0u; at < bytes && result == FX_OK;) {
            uint32_t n = bytes - at;
            if (n > 1024u * 1024u)
                n = 1024u * 1024u;
            result = o->platform->io.cache(o->platform->io.context, (uintptr_t)memory + at, n,
                                           FX_MACHINE_INSTRUCTIONS);
            at += n;
        }
    if (result == FX_OK && (flags & FX_EXECUTE))
        __asm__ volatile("ic ialluis; dsb ish; isb" ::: "memory");
    atomic_store_explicit(&registry->lock, 0u, memory_order_release);
    return result;
}
