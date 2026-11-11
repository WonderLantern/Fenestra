#include "fenestra_memory.h"
#include "fenestra_resident.h"

struct FxResident {
    FxPlatformOwner owner;
    FxServiceOwner service;
    FxNativeBootstrap bootstrap;
    FxNativeExports exports;
    FxResidentHost host;
    FxModule module;
    FxDiscoveredModule entry;
    void *allocation;
    uint8_t *image;
    _Atomic uint32_t phase, error, stop, request_lock, poll_lock, fault_claim;
    uint64_t detail;
    _Atomic uint32_t notified;
};
static FxResult dispatch(void *context, const FxServiceRequest *q, FxServiceReply *a) {
    FxResident *r = context;
    uint64_t before = fx_platform_ui_stamp(&r->owner);
    FxResult result = fx_platform_dispatch(&r->owner, q, a);
    if (r->host.refresh_ui && fx_platform_ui_changed(&r->owner, before))
        r->host.refresh_ui(&r->owner);
    return result;
}
static FxResult stop_services(void *context, uint32_t panic) {
    return fx_platform_stop(&((FxResident *)context)->owner, panic);
}
static FxResult resolve(void *context, const char *name, uint32_t version, FxBindingInfo *out) {
    FxResident *r = context;
    if (!name || !out || !version)
        return FX_ARGUMENT;
    const FxNamedBinding *b = fx_platform_find(&r->owner, name);
    if (!b || b->version < version)
        return FX_CAPABILITY;
    memset(out, 0, sizeof(*out));
    uint32_t n = 0u;
    while (n < sizeof(out->name) && name[n])
        ++n;
    if (n == sizeof(out->name))
        return FX_BOUNDS;
    memcpy(out->name, name, n);
    out->kind = b->kind;
    out->abi = b->abi;
    out->version = b->version;
    out->flags = b->flags;
    out->capabilities = b->capabilities;
    out->bytes = b->bytes;
    out->available = 1u;
    out->address = b->address;
    memcpy(out->original, b->original, sizeof(out->original));
    return FX_OK;
}
static FxResult request(void *context, const FxServiceRequest *q, FxServiceReply *a) {
    FxResident *r = context;
    if (!q || !a)
        return FX_ARGUMENT;
    uint32_t zero = 0u;
    if (!atomic_compare_exchange_strong_explicit(&r->request_lock, &zero, 1u, memory_order_acquire,
                                                 memory_order_relaxed))
        return FX_BUSY;
    FxServiceRequest copy = *q;
    copy.generation = r->owner.generation;
    if (r->service.last_sequence == UINT32_MAX) {
        atomic_store_explicit(&r->request_lock, 0u, memory_order_release);
        return FX_BUDGET;
    }
    copy.sequence = r->service.last_sequence + 1u;
    FxResult result = fx_service_dispatch(&r->service, &copy, a);
    a->sequence = q->sequence;
    atomic_store_explicit(&r->request_lock, 0u, memory_order_release);
    return result;
}
static FxResult record_fault(void *context, FxResult error, uint64_t detail) {
    FxResident *r = context;
    if (error == FX_OK)
        return FX_ARGUMENT;
    uint32_t zero = 0u;
    if (atomic_compare_exchange_strong_explicit(&r->fault_claim, &zero, 1u, memory_order_acq_rel,
                                                memory_order_relaxed)) {
        r->detail = detail;
        atomic_store_explicit(&r->error, (uint32_t)error, memory_order_release);
        atomic_store_explicit(&r->owner.phase, FX_OWNER_FAULT, memory_order_release);
        atomic_store_explicit(&r->stop, 2u, memory_order_release);
        atomic_store_explicit(&r->phase, FX_RESIDENT_FAULT, memory_order_release);
    }
    return error;
}

static FxResult publish(void *context, const FxProviderDeclaration *p, uint64_t *token) {
    FxResident *r = context;
    return fx_provider_publish(fx_native_extensions(&r->owner), &r->owner, p, token);
}
static FxResult unpublish(void *context, uint64_t token) {
    FxResident *r = context;
    return fx_provider_remove(fx_native_extensions(&r->owner), &r->owner, token);
}
static FxResult find_provider(void *context, const FxProviderLookup *p, FxProviderInfo *out) {
    FxResident *r = context;
    return fx_provider_lookup(fx_native_extensions(&r->owner), p, out);
}
static FxResult stream_lookup(void *context, uint64_t token, FxStreamInfo *out) {
    FxResident *r = context;
    return fx_extensions_stream(fx_native_extensions(&r->owner), token, out);
}
static int enter(void *context, uintptr_t address, const FxNativeBootstrap *bootstrap,
                 FxNativeExports *exports) {
    (void)context;
    return ((FxNativeModuleEntry)address)(bootstrap, exports);
}
FxResult fx_resident_start(FxResident **out, const FxPlatform *platform, const FxDiscoveredModule *entry,
                           uint32_t owner, uint32_t generation, const FxModuleDirectory *io,
                           FxResidentHost host) {
    if (!out || *out || !platform || !entry || !io || !io->read || !host.memory.allocate ||
        !host.memory.release || !host.source_memory.allocate || !host.source_memory.release ||
        !host.protect || !entry->enabled || entry->entry_abi != FX_MODULE_ENTRY_NATIVE || !owner ||
        !generation || entry->bytes <= FX_MODULE_HEADER || entry->bytes > FX_NATIVE_MODULE_PACKAGE_LIMIT ||
        !entry->image_bytes || entry->image_bytes > FX_RESIDENT_IMAGE_LIMIT)
        return FX_ARGUMENT;
    uint8_t *source = host.source_memory.allocate(host.source_memory.context, entry->bytes + 1u);
    if (!source)
        return FX_BUDGET;
    uint32_t bytes = 0u;
    FxModule module;
    uint8_t digest[32];
    FxResult result = io->read(io->context, entry->file, source, entry->bytes + 1u, &bytes);
    if (result == FX_OK && bytes != entry->bytes)
        result = FX_INTEGRITY;
    if (result == FX_OK) {
        fx_sha256(source, bytes, digest);
        if (memcmp(digest, entry->digest, 32u))
            result = FX_INTEGRITY;
    }
    if (result == FX_OK)
        result = fx_module_inspect(source, bytes, platform->module_identity, FX_SDK_CAPS, entry->image_bytes,
                                   &module);
    if (result == FX_OK && (module.entry_abi != FX_MODULE_ENTRY_NATIVE || module.bytes != entry->image_bytes))
        result = FX_INTEGRITY;
    FxResident *r = 0;
    if (result == FX_OK) {
        r = host.memory.allocate(host.memory.context, (uint32_t)sizeof(*r));
        if (!r)
            result = FX_BUDGET;
    }
    if (r) {
        memset(r, 0, sizeof(*r));
        r->host = host;
        r->entry = *entry;
        result = fx_platform_init(&r->owner, platform, owner, generation, FX_SDK_CAPS);
        fx_service_init(&r->service, owner, generation, FX_SDK_CAPS,
                        (FxServiceBackend){r, dispatch, stop_services});
        if (result == FX_OK) {
            r->allocation = host.memory.allocate(host.memory.context, entry->image_bytes + 8191u);
            if (!r->allocation)
                result = FX_BUDGET;
        }
    }
    if (result != FX_OK) {
        if (r) {
            if (r->allocation)
                host.memory.release(host.memory.context, r->allocation);
            host.memory.release(host.memory.context, r);
        }
        host.source_memory.release(host.source_memory.context, source);
        return result;
    }
    r->image = (uint8_t *)(((uintptr_t)r->allocation + 4095u) & ~(uintptr_t)4095u);
    uint32_t image_bytes = (entry->image_bytes + 4095u) & ~4095u;
    /* Once native mapping publication starts, retain even a partial failure.
     * Allocator free cannot be called on pages with uncertain permissions. */
    *out = r;
    atomic_store_explicit(&r->phase, FX_RESIDENT_LOADING, memory_order_release);
    result = host.protect(host.context, &r->owner, r->image, image_bytes, FX_READ | FX_WRITE);
    if (result == FX_OK)
        result = fx_module_prepare(source, bytes, digest, platform->module_identity, FX_SDK_CAPS, r->image,
                                   entry->image_bytes, &r->module);
    host.source_memory.release(host.source_memory.context, source);
    if (result == FX_OK)
        result = fx_platform_bind_module(&r->owner, &r->module, r->image, entry->image_bytes);
    for (uint32_t i = 0u; result == FX_OK && i < r->module.segment_count; ++i) {
        FxSegment *s = &r->module.segments[i];
        if (s->flags == (FX_READ | FX_WRITE))
            continue; /* The initial mapping already has this permission. */
        uint32_t first = s->image_offset & ~4095u;
        uint32_t end = (s->image_offset + s->memory_bytes + 4095u) & ~4095u;
        result = host.protect(host.context, &r->owner, r->image + first, end - first, s->flags);
    }
    if (result == FX_OK && host.prepare_owner)
        result = host.prepare_owner(host.context, &r->owner);
    if (result != FX_OK)
        return record_fault(r, result, 0u);
    r->bootstrap = (FxNativeBootstrap){.abi = FX_RESIDENT_ABI,
                                       .bytes = sizeof(FxNativeBootstrap),
                                       .owner = owner,
                                       .generation = generation,
                                       .context = r,
                                       .model = platform->model,
                                       .firmware = platform->firmware,
                                       .resolve = resolve,
                                       .request = request,
                                       .fault = record_fault,
                                       .publish = publish,
                                       .unpublish = unpublish,
                                       .find_provider = find_provider,
                                       .stream = stream_lookup};
    memcpy(r->bootstrap.target, platform->module_identity, 32u);
    r->exports = (FxNativeExports){
        .abi = FX_RESIDENT_ABI, .bytes = sizeof(FxNativeExports), .purpose = FX_MODULE_EXTENSION};
    int code = (host.enter ? host.enter : enter)(host.context, (uintptr_t)r->image + r->module.entry_offset,
                                                 &r->bootstrap, &r->exports);
    /* Initialization may have published arbitrary pointers, even on failure. */
    if (r->exports.abi != FX_RESIDENT_ABI || r->exports.bytes != sizeof(FxNativeExports) ||
        r->exports.purpose < FX_MODULE_APP || r->exports.purpose > FX_MODULE_SERVICE || r->exports.flags) {
        r->exports.notify = 0;
        return record_fault(r, FX_INTEGRITY, (uint64_t)(int64_t)code);
    }
    if (code)
        return record_fault(r, FX_INTEGRITY, (uint64_t)(int64_t)code);
    if (atomic_load_explicit(&r->error, memory_order_acquire))
        return fx_resident_error(r);
    r->owner.kind = r->exports.purpose;
    uint32_t loading = FX_RESIDENT_LOADING;
    if (!atomic_compare_exchange_strong_explicit(&r->phase, &loading, FX_RESIDENT_RUNNING,
                                                 memory_order_release, memory_order_acquire))
        return fx_resident_error(r) == FX_OK ? FX_STATE : fx_resident_error(r);
    return FX_OK;
}
FxPlatformOwner *fx_resident_owner(FxResident *r) { return r ? &r->owner : 0; }
const char *fx_resident_identity(const FxResident *r) { return r ? r->entry.id : 0; }
uint32_t fx_resident_phase(const FxResident *r) {
    return r ? atomic_load_explicit(&r->phase, memory_order_acquire) : FX_RESIDENT_EMPTY;
}
FxResult fx_resident_error(const FxResident *r) {
    return r ? (FxResult)atomic_load_explicit(&r->error, memory_order_acquire) : FX_ARGUMENT;
}
uint64_t fx_resident_detail(const FxResident *r) {
    return r && atomic_load_explicit(&r->error, memory_order_acquire) ? r->detail : 0u;
}
void fx_resident_request_stop(FxResident *r, uint32_t panic) {
    if (!r)
        return;
    uint32_t request_value = panic ? 2u : 1u, old = atomic_load_explicit(&r->stop, memory_order_acquire);
    while (old < request_value &&
           !atomic_compare_exchange_weak_explicit(&r->stop, &old, request_value, memory_order_release,
                                                  memory_order_relaxed)) {
    }
}
FxResult fx_resident_poll(FxResident *r) {
    if (!r)
        return FX_ARGUMENT;
    uint32_t stop = atomic_load_explicit(&r->stop, memory_order_acquire);
    if (!stop)
        return fx_resident_error(r);
    if (fx_resident_phase(r) == FX_RESIDENT_STOPPED && r->notified >= stop)
        return fx_resident_error(r);
    uint32_t zero = 0u;
    if (!atomic_compare_exchange_strong_explicit(&r->poll_lock, &zero, 1u, memory_order_acquire,
                                                 memory_order_relaxed))
        return FX_BUSY;
    atomic_store_explicit(&r->phase, stop == 2u ? FX_RESIDENT_FAULT : FX_RESIDENT_STOPPING,
                          memory_order_release);
    FxResult result = FX_OK;
    if (r->notified < stop && r->exports.notify)
        result = r->exports.notify(r->exports.context, stop == 2u ? FX_RESIDENT_PANIC : FX_RESIDENT_SHUTDOWN);
    if (result == FX_OK)
        r->notified = stop;
    if (result == FX_OK)
        result = fx_platform_stop(&r->owner, stop == 2u);
    if (result == FX_OK)
        atomic_store_explicit(&r->phase, FX_RESIDENT_STOPPED, memory_order_release);
    else if (result != FX_BUSY)
        (void)record_fault(r, result, 0u);
    atomic_store_explicit(&r->poll_lock, 0u, memory_order_release);
    FxResult first = fx_resident_error(r);
    return first == FX_OK ? result : first;
}
