#include "fenestra_loader.h"
#include "fenestra_memory.h"

void fx_loader_init(FxLoader *loader, uint64_t caps, uint64_t budget) {
    if (loader == NULL) return;
    memset(loader, 0, sizeof(*loader));
    fx_init(&loader->runtime, caps, budget);
}

FxResult fx_loader_prepare(FxLoader *loader, const FxCardIO *io, const char *path,
                          uint32_t bytes, const uint8_t digest[32], const uint8_t firmware[32],
                          uint8_t *package, uint32_t package_capacity, uint8_t *image,
                          uint32_t image_capacity, uint64_t now, uint64_t timeout) {
    FxResult result;
    uint8_t pin[32], target[32];
    if (loader == NULL || io == NULL || path == NULL || digest == NULL || firmware == NULL ||
        package == NULL || image == NULL || bytes == 0u || bytes > FX_CARD_LIMIT ||
        bytes > package_capacity || package_capacity > FX_CARD_LIMIT || image_capacity == 0u ||
        image_capacity > FX_MODULE_MEMORY_LIMIT)
        return FX_ARGUMENT;
    memcpy(pin, digest, 32u); memcpy(target, firmware, 32u);
    result = fx_begin(&loader->runtime, 0u, (uint64_t)package_capacity + image_capacity, now, timeout);
    if (result != FX_OK) return result;
    loader->source_reserved=package_capacity;
    result = fx_card_read(io, path, bytes, pin, package, package_capacity, &loader->card);
    if (result != FX_OK) {
        if (loader->card.cleanup != FX_OK) {
            (void)fx_panic(&loader->runtime, result, loader->card.read_calls);
            if (loader->card.cleanup != result)
                (void)fx_panic(&loader->runtime, loader->card.cleanup, 0u);
            return result;
        }
        return fx_reject(&loader->runtime, result, loader->card.read_calls);
    }
    result = fx_module_prepare(package, loader->card.bytes, pin, target,
                               loader->runtime.verified_capabilities, image, image_capacity, &loader->module);
    if (result != FX_OK) return fx_reject(&loader->runtime, result, 0u);
    loader->image = image;
    memcpy(loader->package_sha256, pin, 32u);
    memcpy(loader->target_firmware_sha256, target, 32u);
    return fx_prepared(&loader->runtime, now);
}

FxResult fx_loader_source_released(FxLoader *loader) {
    if(!loader)return FX_ARGUMENT;
    if(loader->runtime.phase!=FX_PREPARED || !loader->module.prepared || !loader->image ||
       loader->exec_arena || loader->runtime.outstanding)return FX_STATE;
    if(loader->source_reserved>loader->runtime.reserved_bytes)return FX_INTEGRITY;
    loader->runtime.reserved_bytes-=loader->source_reserved;loader->source_reserved=0u;
    return FX_OK;
}

static FxResult make_visible(uint8_t *image, uint32_t bytes) {
#if defined(__aarch64__)
    uint64_t el, control, ctr, line, address, end = (uint64_t)(uintptr_t)image + bytes;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    if (el == 12u) __asm__ volatile("mrs %0, sctlr_el3" : "=r"(control));
    else if (el == 8u) __asm__ volatile("mrs %0, sctlr_el2" : "=r"(control));
    else if (el == 4u) __asm__ volatile("mrs %0, sctlr_el1" : "=r"(control));
    else return FX_CAPABILITY;
    if ((control & 1u) != 0u) return FX_CAPABILITY;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line = 4u << ((ctr >> 16) & 15u);
    for (address = (uintptr_t)image & ~(line - 1u); address < end; address += line)
        __asm__ volatile("dc cvau, %0" : : "r"(address) : "memory");
    __asm__ volatile("dsb ish" : : : "memory");
    line = 4u << (ctr & 15u);
    for (address = (uintptr_t)image & ~(line - 1u); address < end; address += line)
        __asm__ volatile("ic ivau, %0" : : "r"(address) : "memory");
    __asm__ volatile("dsb ish; isb" : : : "memory");
    return FX_OK;
#else
    (void)image; (void)bytes;
    return FX_CAPABILITY;
#endif
}

FxResult fx_loader_activate(FxLoader *loader, uint64_t now) {
    FxResult result;
    if (loader == NULL) return FX_ARGUMENT;
    if (loader->runtime.phase != FX_PREPARED || !loader->module.prepared || loader->image == NULL)
        return FX_STATE;
    result = make_visible(loader->image, loader->module.bytes);
    if (result != FX_OK) return result;
    return fx_publish(&loader->runtime, now);
}

FxResult fx_loader_activate_mapped(FxLoader *loader, FxExecArena *arena, uint64_t now) {
    FxResult result;
    if (loader == NULL || arena == NULL) return FX_ARGUMENT;
    if (loader->runtime.phase != FX_PREPARED || !loader->module.prepared || loader->image == NULL ||
        loader->exec_arena != NULL || (uintptr_t)loader->image != arena->virtual_base) return FX_STATE;
    result = fx_poll(&loader->runtime, now); if (result != FX_OK) return result;
    result = fx_exec_publish(arena, &loader->module); if (result != FX_OK) return result;
    loader->exec_arena = arena;
    return fx_publish(&loader->runtime, now);
}

FxResult fx_loader_confirm_quiesced(FxLoader *loader, uint32_t generation, uint64_t now) {
    FxResult result;
    if (loader == NULL) return FX_ARGUMENT;
    if (generation != loader->runtime.generation) return FX_STALE;
    if (loader->runtime.phase != FX_QUIESCING) return FX_STATE;
    if (loader->exec_arena != NULL && loader->exec_arena->bound) {
        if (loader->runtime.outstanding != 0u) return FX_BUSY;
        result = fx_poll(&loader->runtime, now); if (result != FX_OK) return result;
        result = fx_exec_revoke(loader->exec_arena);
        if (result != FX_OK) return fx_panic(&loader->runtime, result, 0u);
    }
    return fx_confirm_quiesced(&loader->runtime, generation, now);
}
int fx_loader_release_allowed(const FxLoader *loader) {
    return loader != NULL && fx_release_allowed(&loader->runtime) &&
           (loader->exec_arena == NULL || (!loader->exec_arena->bound && !loader->exec_arena->published));
}

FxResult fx_loader_invoke(FxLoader *loader, uint32_t generation, const FxModuleServices *services,
                         uint64_t now, int *module_result) {
    FxLease lease;
    FxResult result;
    if (module_result != NULL) *module_result = 0;
    if (loader == NULL || services == NULL || module_result == NULL || services->abi != FX_ABI ||
        services->bytes < sizeof(*services)) return FX_ARGUMENT;
    if (!loader->module.prepared || loader->image == NULL) return FX_STATE;
#if defined(__aarch64__)
    if (loader->exec_arena != NULL) {
        result = fx_exec_live(loader->exec_arena);
        if (result != FX_OK) return fx_panic(&loader->runtime, result, 0u);
        if (!loader->exec_arena->published) return FX_STATE;
    }
    result = fx_acquire(&loader->runtime, generation, now, &lease);
    if (result != FX_OK) return result;
    *module_result = ((FxModuleEntry)(uintptr_t)(loader->image + loader->module.entry_offset))(services);
    return fx_release(&loader->runtime, lease, now);
#else
    (void)generation; (void)now; (void)lease; (void)result;
    return FX_CAPABILITY;
#endif
}
