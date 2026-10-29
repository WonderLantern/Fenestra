#include "fenestra_module.h"
#include "fenestra_memory.h"

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t)u32(p + 4u) << 32; }
static int span(uint32_t offset, uint32_t bytes, uint32_t limit) {
    return offset <= limit && bytes <= limit - offset;
}
static int overlap(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
    return (uint64_t)a < (uint64_t)b + bn && (uint64_t)b < (uint64_t)a + an;
}
static int in_segment(const FxModule *module, uint32_t offset, uint32_t bytes,
                      uint32_t permissions, int file_backed) {
    uint32_t i;
    for (i = 0u; i < module->segment_count; ++i) {
        const FxSegment *s = &module->segments[i];
        uint32_t length = file_backed ? s->file_bytes : s->memory_bytes;
        if (offset >= s->image_offset && span(offset - s->image_offset, bytes, length) &&
            (s->flags & permissions) == permissions) return 1;
    }
    return 0;
}

FxResult fx_module_inspect(const uint8_t *package,uint32_t size,const uint8_t firmware[32],
                           uint64_t caps,uint32_t capacity,FxModule *out) {
    if(!out)return FX_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!package || !firmware || size<FX_MODULE_HEADER || size>FX_CARD_LIMIT)return FX_ARGUMENT;
    FxModule candidate;uint8_t hash[32];uint32_t i,j,tables_end,relocations_start;
    uint32_t entry_abi=fx_module_header_entry(package,size);
    if (memcmp(package, "FENESTRA", 8u) != 0 || entry_abi==UINT32_MAX ||
        u32(package + 12u) != FX_MODULE_HEADER || u32(package + 88u) != size)
        return FX_BOUNDS;
    if (u32(package + 16u) != FX_ABI || u32(package + 20u) != FX_ISA_AARCH64 ||
        memcmp(package + 24u, firmware, 32u) != 0) return FX_CAPABILITY;
    fx_sha256(package + FX_MODULE_HEADER, size - FX_MODULE_HEADER, hash);
    if (memcmp(hash, package + 56u, 32u) != 0) return FX_INTEGRITY;
    memset(&candidate, 0, sizeof(candidate));
    candidate.stacks=(FxModuleStacks){FX_MODULE_STACK_DEFAULT,FX_MODULE_STACK_DEFAULT};
    if(u32(package+8u)==3u) {
        candidate.stacks=(FxModuleStacks){u32(package+120u),u32(package+124u)};
        if(!fx_module_stacks_valid(candidate.stacks))return FX_BOUNDS;
    }
    candidate.abi = u32(package + 16u);
    candidate.entry_abi=entry_abi;
    candidate.isa = u32(package + 20u);
    candidate.bytes = u32(package + 92u);
    candidate.entry_offset = u32(package + 96u);
    candidate.segment_count = u32(package + 100u);
    candidate.relocation_count = u32(package + 104u);
    candidate.required_capabilities = u64(package + 108u);
    if ((candidate.required_capabilities & ~caps) != 0u) return FX_CAPABILITY;
    if (candidate.bytes == 0u || candidate.bytes > capacity || candidate.bytes > FX_MODULE_MEMORY_LIMIT ||
        candidate.segment_count == 0u || candidate.segment_count > FX_MODULE_SEGMENTS ||
        candidate.relocation_count > FX_MODULE_RELOCATIONS) return FX_BOUNDS;
    relocations_start = FX_MODULE_HEADER + candidate.segment_count * 24u;
    tables_end = relocations_start + candidate.relocation_count * 16u;
    if (tables_end > size) return FX_BOUNDS;
    for (i = 0u; i < candidate.segment_count; ++i) {
        const uint8_t *p = package + FX_MODULE_HEADER + i * 24u;
        FxSegment *s = &candidate.segments[i];
        s->image_offset = u32(p); s->file_offset = u32(p + 4u);
        s->file_bytes = u32(p + 8u); s->memory_bytes = u32(p + 12u);
        s->flags = u32(p + 16u); s->alignment = u32(p + 20u);
        if (s->memory_bytes == 0u || s->file_bytes > s->memory_bytes ||
            !span(s->image_offset, s->memory_bytes, candidate.bytes) ||
            s->file_offset < tables_end || !span(s->file_offset, s->file_bytes, size) ||
            (s->flags & ~7u) != 0u || (s->flags & FX_READ) == 0u ||
            (s->flags & (FX_WRITE | FX_EXECUTE)) == (FX_WRITE | FX_EXECUTE) ||
            s->alignment == 0u || s->alignment > 4096u ||
            (s->alignment & (s->alignment - 1u)) != 0u ||
            (s->image_offset & (s->alignment - 1u)) != 0u)
            return FX_BOUNDS;
        for (j = 0u; j < i; ++j) {
            const FxSegment *other = &candidate.segments[j];
            if (overlap(s->image_offset, s->memory_bytes, other->image_offset, other->memory_bytes) ||
                (s->file_bytes != 0u && other->file_bytes != 0u &&
                 overlap(s->file_offset, s->file_bytes, other->file_offset, other->file_bytes)))
                return FX_BOUNDS;
            if (s->flags != other->flags &&
                overlap(s->image_offset & ~4095u,
                        ((s->image_offset + s->memory_bytes + 4095u) & ~4095u) - (s->image_offset & ~4095u),
                        other->image_offset & ~4095u,
                        ((other->image_offset + other->memory_bytes + 4095u) & ~4095u) - (other->image_offset & ~4095u)))
                return FX_BOUNDS;
        }
    }
    if ((candidate.entry_offset & 3u) != 0u ||
        !in_segment(&candidate, candidate.entry_offset, 4u, FX_EXECUTE, 1)) return FX_BOUNDS;
    for (i = 0u; i < candidate.relocation_count; ++i) {
        const uint8_t *p = package + relocations_start + i * 16u;
        uint32_t offset = u32(p), kind = u32(p + 4u);
        uint64_t value = u64(p + 8u);
        if (kind != 1u || (offset & 7u) != 0u ||
            !in_segment(&candidate, offset, 8u, FX_WRITE, 0) ||
            value > UINT32_MAX || !in_segment(&candidate, (uint32_t)value, 1u, FX_READ, 0)) return FX_BOUNDS;
        for (j = 0u; j < i; ++j)
            if (u32(package + relocations_start + j * 16u) == offset) return FX_BOUNDS;
    }
    /* Metadata only: no image, relocation, mapping or executable publication. */
    *out=candidate;return FX_OK;
}

FxResult fx_module_prepare(const uint8_t *package, uint32_t size,
                          const uint8_t expected_digest[32], const uint8_t firmware[32],
                          uint64_t caps, uint8_t *image, uint32_t capacity, FxModule *out) {
    FxModule candidate;
    uint8_t hash[32];
    uint32_t i, j, relocations_start;
    uintptr_t source_address, destination_address;
    if (out == NULL) return FX_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (package == NULL || image == NULL || expected_digest == NULL || firmware == NULL ||
        size < FX_MODULE_HEADER || size > FX_CARD_LIMIT) return FX_ARGUMENT;
    source_address = (uintptr_t)package;
    destination_address = (uintptr_t)image;
    if ((destination_address & 4095u) != 0u || size > UINTPTR_MAX - source_address || capacity > UINTPTR_MAX - destination_address ||
        (source_address < destination_address + capacity && destination_address < source_address + size))
        return FX_ARGUMENT;
    fx_sha256(package, size, hash);
    if (memcmp(hash, expected_digest, 32u) != 0) return FX_INTEGRITY;
    FxResult result=fx_module_inspect(package,size,firmware,caps,capacity,&candidate);
    if(result!=FX_OK)return result;
    relocations_start=FX_MODULE_HEADER+candidate.segment_count*24u;
    /* No image mutation happens until all descriptors and relocations pass. */
    memset(image, 0, candidate.bytes);
    for (i = 0u; i < candidate.segment_count; ++i) {
        const FxSegment *s = &candidate.segments[i];
        memcpy(image + s->image_offset, package + s->file_offset, s->file_bytes);
    }
    for (i = 0u; i < candidate.relocation_count; ++i) {
        const uint8_t *p = package + relocations_start + i * 16u;
        uint64_t value = (uint64_t)destination_address + u64(p + 8u);
        uint32_t offset = u32(p);
        for (j = 0u; j < 8u; ++j) image[offset + j] = (uint8_t)(value >> (j * 8u));
    }
    candidate.prepared = 1u;
    *out = candidate;
    return FX_OK;
}
