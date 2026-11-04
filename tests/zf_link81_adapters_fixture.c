#include "card_read.h"
#include "module_controller.h"
#include <assert.h>
#include <string.h>

FxResult fx_zf_directory_next(void *, FxModuleDirectoryEntry *);
static uint32_t calls, mode;
static uint8_t output[1600];

static uint32_t read_native(int32_t fd, void *data, uint32_t bytes, uint32_t *done) {
    assert(fd == 7 && bytes && bytes <= 511u);
    assert(data == output + calls * 511u);
    ++calls;
    *done = mode == 1u ? 13u : mode == 2u ? 0u : mode == 4u ? bytes + 1u : bytes;
    if(mode != 4u)memset(data, 0x5a, *done);
    return mode == 3u ? 0x80000007u : 0u;
}
static FxResult next_native(void *context, FxModuleDirectoryEntry *entry) {
    assert(context == &mode);
    if(mode == 1u)return FX_EMPTY;
    if(mode == 2u)return FX_IO;
    if(mode == 3u)memset(entry->name, 'A', sizeof(entry->name));
    else strcpy(entry->name, "DEMO-ZF.WLM");
    entry->bytes = 7647u;
    return FX_OK;
}
int main(void) {
    uint32_t done;
    for(mode = 0u; mode < 5u; ++mode) {
        calls = 0u;memset(output, 0, sizeof(output));
        uint32_t result = fx_zf_card_read_cached(read_native, 7, output, 1536u, &done);
        if(mode == 0u)assert(!result && done == 1536u && calls == 4u);
        if(mode == 1u)assert(!result && done == 13u && calls == 1u);
        if(mode == 2u)assert(!result && !done && calls == 1u);
        if(mode == 3u)assert(result == 0x80000007u && done == 511u && calls == 1u);
        if(mode == 4u)assert(result && !done && calls == 1u);
        assert(output[1536] == 0u);
    }
    calls = 0u;
    assert(!fx_zf_card_read_cached(read_native, 7, 0, 0, &done) && !done && !calls);
    assert(fx_zf_card_read_cached(0, 7, output, 1, &done) && !done);
    assert(fx_zf_card_read_cached(read_native, 7, 0, 1, &done) && !done);
    assert(fx_zf_card_read_cached(read_native, 7, output, 1, 0));

    FxZfModuleController controller = {0};
    controller.source_directory.context = &mode;
    controller.source_directory.next = next_native;
    for(mode = 0u; mode < 4u; ++mode) {
        FxModuleDirectoryEntry entry = {0};
        strcpy(entry.name, "UNCHANGED.WLM");
        controller.stage = 99u;controller.directory_bytes = 123u;
        FxResult result = fx_zf_directory_next(&controller, &entry);
        if(mode == 0u) {
            assert(result == FX_OK && !strcmp(entry.name, "demo-zf.wlm"));
            assert(controller.stage == FX_ZF_LOAD_DIRECTORY_ENTRY && controller.directory_bytes == 7647u);
        } else if(mode == 3u) {
            for(unsigned i = 0u; i < sizeof(entry.name); ++i)assert(entry.name[i] == 'a');
            assert(entry.bytes == 7647u);
        } else {
            assert(result == (mode == 1u ? FX_EMPTY : FX_IO));
            assert(!strcmp(entry.name, "UNCHANGED.WLM"));
            assert(controller.stage == FX_ZF_LOAD_DIRECTORY_NEXT && controller.directory_bytes == 123u);
        }
    }
    return 0;
}
