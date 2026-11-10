#define _GNU_SOURCE
#include "../src/platform/zf_c301/address_space.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

typedef struct { uintptr_t tables, mapped; } ProbeRanges;

static int probe(void *context, uintptr_t address, uint32_t flags) {
    const ProbeRanges *ranges = context;
    int owned = (address >= ranges->tables && address < ranges->tables + 16u * 4096u) ||
                (address >= ranges->mapped && address < ranges->mapped + 0x2000u);
    return owned && (flags & FX_ZF_MAP_READ);
}

int main(void) {
    size_t table_bytes = 16u * 4096u;
    void *tables = mmap(NULL, table_bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(tables != MAP_FAILED);
    FxZfAddressSpace space = {0};
    uintptr_t root = 0;
    ProbeRanges ranges = {(uintptr_t)tables, 0x40000000u};
    assert(fx_zf_space_init(&space, tables, 16u, &ranges, probe) == FX_OK);
    assert(fx_zf_space_map(&space, 0x40000000u, 0x2000u, FX_ZF_MAP_READ | FX_ZF_MAP_EXECUTE) == FX_OK);
    assert(fx_zf_space_map(&space, 0x40000000u, 0x1000u, FX_ZF_MAP_READ) == FX_INTEGRITY);
    assert(fx_zf_space_seal(&space, &root) == FX_STATE);
    assert(munmap(tables, table_bytes) == 0);
    return 0;
}
