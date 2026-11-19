#include "fenestra_memory.h"
#include "fenestra_native_sdk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static FxExtensionDomain *domain;
FxExtensionDomain *fx_native_extensions(FxPlatformOwner *o) {
    (void)o;
    return domain;
}
static uint8_t package[188];
static uint32_t protections, entries, notifications, reads;
static const FxNativeBootstrap *saved;
static void *allocations[256];
static uint32_t count;
static void *allocate(void *context, uint32_t bytes) {
    (void)context;
    void *p = malloc(bytes);
    assert(p && count < 256u);
    allocations[count++] = p;
    return p;
}
static void release(void *context, void *p) {
    (void)context;
    for (uint32_t i = 0u; i < count; ++i)
        if (allocations[i] == p) {
            allocations[i] = 0;
            free(p);
            return;
        }
    assert(0);
}
static uint64_t add(uint64_t a, uint64_t b) { return a + b; }
static FxResult protect(void *context, FxPlatformOwner *o, uint8_t *p, uint32_t bytes, uint32_t flags) {
    (void)context;
    (void)o;
    assert(p && bytes && !((uintptr_t)p & 4095u) && !(bytes & 4095u));
    assert((flags & 6u) != 6u);
    if (!protections)
        assert(flags == (FX_READ | FX_WRITE));
    ++protections;
    return FX_OK;
}
static FxResult read_file(void *context, const char *name, uint8_t *out, uint32_t capacity, uint32_t *bytes) {
    (void)context;
    (void)name;
    assert(capacity > sizeof(package));
    memcpy(out, package, sizeof(package));
    *bytes = sizeof(package);
    ++reads;
    return FX_OK;
}
static FxResult stop(void *context, uint32_t event) {
    (void)context;
    assert(event == FX_RESIDENT_SHUTDOWN || event == FX_RESIDENT_PANIC);
    return ++notifications == 1u ? FX_BUSY : FX_OK;
}
static int entry(void *context, uintptr_t address, const FxNativeBootstrap *api, FxNativeExports *out) {
    (void)context;
    assert(address && protections == 2u && api->abi == FX_RESIDENT_ABI);
    ++entries;
    saved = api;
    FxBindingInfo info;
    assert(api->resolve(api->context, "test.add", 1u, &info) == FX_OK);
    assert(((uint64_t (*)(uint64_t, uint64_t))(uintptr_t)info.address)(2u, 3u) == 5u);
    FxUiControl root = {.abi = 1u,
                        .bytes = sizeof(root),
                        .id = 1u,
                        .kind = FX_UI_PAGE,
                        .flags = FX_UI_ROOT | FX_UI_VISIBLE,
                        .label = "Native test"};
    assert(fx_native_ui_put(api, &root) == FX_OK); /* Actual optional direct SDK dispatch. */
    out->purpose = FX_MODULE_SERVICE;
    out->notify = stop;
    return 0;
}
static void word(uint8_t *buffer, uint32_t at, uint32_t v) {
    buffer[at] = (uint8_t)v;
    buffer[at + 1u] = (uint8_t)(v >> 8);
    buffer[at + 2u] = (uint8_t)(v >> 16);
    buffer[at + 3u] = (uint8_t)(v >> 24);
}
static void put(uint32_t at, uint32_t v) { word(package, at, v); }
static void make(uint8_t identity, FxDiscoveredModule *discovered) {
    memset(package, 0, sizeof(package));
    memcpy(package, "FENESTRA", 8u);
    put(8u, 4u);
    put(12u, 128u);
    put(16u, 1u);
    put(20u, 1u);
    package[24] = identity;
    put(88u, sizeof(package));
    put(92u, 8192u);
    put(100u, 2u);
    put(116u, FX_MODULE_ENTRY_NATIVE);
    put(128u, 0u);
    put(132u, 176u);
    put(136u, 4u);
    put(140u, 4u);
    put(144u, FX_READ | FX_EXECUTE);
    put(148u, 4096u);
    put(152u, 4096u);
    put(156u, 180u);
    put(160u, 8u);
    put(164u, 4096u);
    put(168u, FX_READ | FX_WRITE);
    put(172u, 4096u);
    put(176u, UINT32_C(0xd65f03c0));
    fx_sha256(package + 128u, sizeof(package) - 128u, package + 56u);
    *discovered = (FxDiscoveredModule){.id = "native_test",
                                       .file = "native_test.wlm",
                                       .enabled = 1u,
                                       .entry_abi = FX_MODULE_ENTRY_NATIVE,
                                       .bytes = sizeof(package),
                                       .image_bytes = 8192u};
    fx_sha256(package, sizeof(package), discovered->digest);
}
static void large_image(void) {
    const uint32_t relocations = 256u, payload = 2u * 1024u * 1024u;
    const uint32_t data = 176u + relocations * 16u, bytes = data + 4u + payload;
    const uint32_t image_bytes = 6u * 1024u * 1024u + 4096u;
    uint8_t *source = calloc(1u, bytes), *allocation = malloc(image_bytes + 4095u);
    assert(source && allocation);
    uint8_t *image = (void *)(((uintptr_t)allocation + 4095u) & ~(uintptr_t)4095u);
    FxDiscoveredModule ignored;
    make(1u, &ignored);
    memcpy(source, package, 176u);
    word(source, 88u, bytes);
    word(source, 92u, image_bytes);
    word(source, 104u, relocations);
    word(source, 132u, data);
    word(source, 156u, data + 4u);
    word(source, 160u, payload);
    word(source, 164u, image_bytes - 4096u);
    word(source, data, UINT32_C(0xd65f03c0));
    memset(source + data + 4u, 0x5a, payload);
    for (uint32_t i = 0u; i < relocations; ++i) {
        word(source, 176u + i * 16u, 4096u + i * 8u);
        word(source, 180u + i * 16u, 1u);
    }
    fx_sha256(source + 128u, bytes - 128u, source + 56u);
    uint8_t identity[32] = {1u}, digest[32];
    fx_sha256(source, bytes, digest);
    FxModule m;
    assert(fx_module_prepare(source, bytes, digest, identity, FX_SDK_CAPS, image, image_bytes, &m) == FX_OK);
    uint64_t relocated;
    memcpy(&relocated, image + 4096u + 255u * 8u, sizeof(relocated));
    assert(relocated == (uintptr_t)image && image[4096u + payload - 1u] == 0x5a);
    assert(!image[4096u + payload] && !image[image_bytes - 1u]);
    word(source, 176u + 16u, 4096u); /* Duplicate/unsorted target. */
    fx_sha256(source + 128u, bytes - 128u, source + 56u);
    assert(fx_module_inspect(source, bytes, identity, FX_SDK_CAPS, image_bytes, &m) == FX_BOUNDS);
    free(allocation);
    free(source);
}
int main(void) {
    large_image();
    domain = fx_extensions_create((FxIndexMemory){0, allocate, release}, 1024u * 1024u);
    assert(domain);
    for (uint32_t target = 1u; target <= 2u; ++target) {
        FxNamedBinding binding = {.name = "test.add",
                                  .kind = FX_BIND_FUNCTION,
                                  .abi = FX_CALL_U64_8,
                                  .version = 1u,
                                  .address = (uintptr_t)add};
        FxPlatform platform = {.abi = 1u,
                               .count = 1u,
                               .model = target == 1u ? "ZR fixture" : "Z9 fixture",
                               .firmware = "fixture",
                               .bindings = &binding};
        platform.module_identity[0] = (uint8_t)target;
        FxDiscoveredModule discovered;
        make((uint8_t)target, &discovered);
        FxResident *r = 0;
        protections = entries = notifications = 0u;
        FxResidentHost host = {.memory = {0, allocate, release},
                               .source_memory = {0, allocate, release},
                               .protect = protect,
                               .enter = entry};
        FxModuleDirectory io = {.read = read_file};
        assert(fx_resident_start(&r, &platform, &discovered, 1u, 1u, &io, host) == FX_OK && r &&
               entries == 1u);
        assert(fx_resident_phase(r) == FX_RESIDENT_RUNNING &&
               fx_resident_owner(r)->kind == FX_MODULE_SERVICE);
        assert(fx_resident_poll(r) == FX_OK && entries == 1u);
        assert(fx_resident_start(&r, &platform, &discovered, 1u, 1u, &io, host) == FX_ARGUMENT);
        fx_resident_request_stop(r, 0u);
        assert(fx_resident_poll(r) == FX_BUSY);
        assert(fx_resident_poll(r) == FX_OK);
        assert(fx_resident_phase(r) == FX_RESIDENT_STOPPED && fx_resident_poll(r) == FX_OK &&
               notifications == 2u);
        fx_resident_request_stop(r, 1u);
        assert(fx_resident_poll(r) == FX_OK && notifications == 3u);
        r = 0;
        protections = entries = notifications = 0u;
        assert(fx_resident_start(&r, &platform, &discovered, 1u, 2u, &io, host) == FX_OK);
        assert(saved->fault(saved->context, FX_IO, 77u) == FX_IO);
        assert(saved->fault(saved->context, FX_TIMEOUT, 88u) == FX_TIMEOUT);
        assert(fx_resident_error(r) == FX_IO && fx_resident_detail(r) == 77u);
        r = 0;
        discovered.digest[0] ^= 1u;
        protections = entries = 0u;
        assert(fx_resident_start(&r, &platform, &discovered, 1u, 3u, &io, host) == FX_INTEGRITY && !r &&
               !protections && !entries);
    }
    assert(reads == 6u);
    for (uint32_t i = 0u; i < count; ++i)
        if (allocations[i])
            free(allocations[i]);
    puts("resident runtime: dual identities, mapping order, direct native calls, UI, once-only init, stop "
         "and first fault passed");
    return 0;
}
