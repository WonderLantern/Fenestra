#include "fenestra_extensions.h"
#include "fenestra_hooks.h"
#include "fenestra_module_bus.h"
#include "fenestra_native_mapping.h"
#include "fenestra_resources.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t card[65536], image[65536];
static uint32_t position, flags_seen, renamed, closed, close_failed, directory_step;
static void *allocate(uint32_t bytes, uint32_t pool) {
    (void)pool;
    return malloc(bytes);
}
static void release(void *p) { free(p); }
static int32_t open_file(const char *name, uint32_t flags, uint32_t attributes) {
    assert(!strcmp(name, "A:\\FENESTRA\\config.dat"));
    assert(attributes == 7u);
    flags_seen = flags;
    position = 0u;
    return 3;
}
static uint32_t read_file(int32_t fd, void *out, uint32_t bytes, uint32_t *done) {
    assert(fd == 3);
    *done = bytes;
    if (bytes > sizeof(card) - position)
        *done = (uint32_t)sizeof(card) - position;
    memcpy(out, card + position, *done);
    position += *done;
    return 0u;
}
static uint32_t write_file(int32_t fd, const void *in, uint32_t bytes, uint32_t *done, void *context) {
    assert(fd == 3 && !context);
    *done = bytes;
    if (bytes > sizeof(card) - position)
        *done = (uint32_t)sizeof(card) - position;
    memcpy(card + position, in, *done);
    position += *done;
    return 0u;
}
static uint32_t close_file(int32_t fd) {
    assert(fd == 3 || fd == 9);
    ++closed;
    return close_failed;
}
static uint32_t rename_file(const char *from, const char *to) {
    assert(!strcmp(from, "A:\\FENESTRA\\config.dat"));
    assert(!strcmp(to, "A:\\FENESTRA\\saved.dat"));
    ++renamed;
    return 0u;
}
static uint32_t directory_open(const char *path, int32_t *fd) {
    assert(!strcmp(path, "A:\\FENESTRA"));
    *fd = 9;
    directory_step = 0u;
    return 0u;
}
static uint32_t directory_next(int32_t fd, void *data) {
    assert(fd == 9);
    if (directory_step++)
        return UINT32_C(0x80000030);
    uint8_t *p = data;
    memset(p, 0, 0x220);
    p[14] = 'x';
    p[16] = 0xa9;
    p[17] = 3;
    p[0x218] = 123u;
    return 0u;
}
FxExtensionDomain *fx_native_extensions(FxPlatformOwner *o) {
    (void)o;
    return 0;
}
void fx_native_module_fault(FxPlatformOwner *o, FxResult r) {
    (void)o;
    (void)r;
    assert(0);
}
FxHookRegistry *fx_native_hooks(void) { return 0; }
FxNativeMappingRegistry *fx_native_mappings(void) { return 0; }
FxModuleBus *fx_native_module_bus(void) { return 0; }
FxResult fx_native_hook_page(FxPlatformOwner *o, uint8_t *p, FxHookCell *c, uint32_t *retained) {
    (void)o;
    (void)p;
    (void)c;
    *retained = 0u;
    return FX_CAPABILITY;
}
static FxResult resource(void *ctx, uint32_t operation, const FxNamedBinding *binding, FxPlatformOwner *owner,
                         uint64_t token, uint64_t *cookie, const FxServiceRequest *q, FxServiceReply *a) {
    return fx_native_resource(ctx, operation, binding, owner, token, cookie, q, a);
}
#define FUNCTION(n, f)                                                                                       \
    {.name = n, .kind = FX_BIND_FUNCTION, .abi = FX_CALL_U64_8, .version = 1u, .address = (uintptr_t)f}
static FxResult call(FxPlatformOwner *owner, uint64_t token, uint32_t operation, uint64_t arg,
                     const void *data, uint32_t bytes, FxServiceReply *out) {
    FxServiceRequest q = {
        .operation = FX_S_PLATFORM, .bytes = bytes, .arg = {FX_BIND_CALL, token, operation, arg}};
    if (bytes)
        memcpy(q.data, data, bytes);
    memset(out, 0, sizeof(*out));
    return fx_platform_dispatch(owner, &q, out);
}
int main(void) {
    FxNamedBinding bindings[] = {
        FUNCTION("rtos.memory.allocate", allocate),
        FUNCTION("rtos.memory.release", release),
        FUNCTION("camera.card.open", open_file),
        FUNCTION("camera.card.read", read_file),
        FUNCTION("camera.card.write", write_file),
        FUNCTION("camera.card.close", close_file),
        FUNCTION("camera.card.rename", rename_file),
        FUNCTION("camera.card.directory.open", directory_open),
        FUNCTION("camera.card.directory.next", directory_next),
        FUNCTION("camera.card.directory.close", close_file),
        {.name = "card.files", .kind = FX_BIND_DEVICE, .abi = FX_CALL_DEVICE, .version = 1u, .address = 1u}};
    FxPlatform p = {.abi = 1u,
                    .count = sizeof(bindings) / sizeof(bindings[0]),
                    .bindings = bindings,
                    .io = {.resource = resource}};
    FxPlatformOwner owner = {0};
    assert(fx_platform_init(&owner, &p, 1u, 1u, FX_SDK_CAPS) == FX_OK);
    owner.module_image = image;
    owner.module_bytes = sizeof(image);
    owner.module_segment_count = 1u;
    owner.module_segments[0] = (FxSegment){.memory_bytes = sizeof(image), .flags = FX_READ | FX_WRITE};
    FxServiceRequest open = {.operation = FX_S_PLATFORM, .bytes = 64u, .arg = {FX_BIND_OPEN, 1u}};
    memcpy(open.data, "card.files", 11u);
    FxServiceReply out;
    assert(fx_platform_dispatch(&owner, &open, &out) == FX_OK);
    uint64_t binding = out.value[0];
    FxFileOriginalOpen original = {
        .file = {.mode = FX_FILE_ORIGINAL_FLAGS, .name = "config.dat"}, .flags = 2u, .attributes = 7u};
    assert(call(&owner, binding, FX_FILE_OPEN, 0u, &original, sizeof(original), &out) == FX_OK &&
           flags_seen == 2u);
    uint64_t file = out.value[0];
    for (uint32_t i = 0u; i < sizeof(image); ++i)
        image[i] = (uint8_t)(i * 13u);
    FxFileModuleRequest bulk = {file, (uintptr_t)image, sizeof(image), 0u};
    assert(call(&owner, binding, FX_FILE_WRITE_MODULE, 0u, &bulk, sizeof(bulk), &out) == FX_OK &&
           out.value[0] == sizeof(image));
    assert(!memcmp(card, image, sizeof(card)));
    position = 0u;
    memset(image, 0, sizeof(image));
    assert(call(&owner, binding, FX_FILE_READ_MODULE, 0u, &bulk, sizeof(bulk), &out) == FX_OK &&
           !memcmp(card, image, sizeof(card)));
    bulk.address = (uintptr_t)image + 1u;
    assert(call(&owner, binding, FX_FILE_READ_MODULE, 0u, &bulk, sizeof(bulk), &out) == FX_BOUNDS);
    FxFileRequest close = {.handle = file};
    assert(call(&owner, binding, FX_FILE_CLOSE, 0u, &close, sizeof(close), &out) == FX_OK && closed == 1u);
    FxFileRename rename = {.from = "config.dat", .to = "saved.dat"};
    assert(call(&owner, binding, FX_FILE_RENAME, 0u, &rename, sizeof(rename), &out) == FX_OK &&
           renamed == 1u);
    assert(call(&owner, binding, FX_FILE_DIRECTORY_OPEN, 0u, 0, 0u, &out) == FX_OK);
    uint64_t directory = out.value[0];
    assert(call(&owner, binding, FX_FILE_DIRECTORY_NEXT, directory, 0, 0u, &out) == FX_OK);
    FxFileDirectoryEntry entry;
    memcpy(&entry, out.data, sizeof(entry));
    assert(entry.bytes == 123u && entry.name[0] == 'x' && entry.name[1] == 0x3a9u);
    assert(call(&owner, binding, FX_FILE_DIRECTORY_NEXT, directory, 0, 0u, &out) == FX_EMPTY);
    close_failed = 1u;
    assert(call(&owner, binding, FX_FILE_DIRECTORY_CLOSE, directory, 0, 0u, &out) == FX_CLOSE);
    close_failed = 0u;
    assert(call(&owner, binding, FX_FILE_DIRECTORY_CLOSE, directory, 0, 0u, &out) == FX_CLOSE &&
           closed == 2u);
    puts("native file extensions: original flags, bulk IO, bounds, Unicode directory, rename and uncertain "
         "close passed");
    return 0;
}
