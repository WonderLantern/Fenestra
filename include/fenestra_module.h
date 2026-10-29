#ifndef FENESTRA_MODULE_H
#define FENESTRA_MODULE_H
#include "fenestra.h"

#define FX_MODULE_HEADER 128u
#define FX_MODULE_SEGMENTS 8u
#define FX_MODULE_RELOCATIONS 128u
#define FX_MODULE_MEMORY_LIMIT (4u * 1024u * 1024u)
/* Native package/image ceilings match the format; allocations use actual
 * preflight sizes, never a maximum-size buffer for every installed file. */
#define FX_NATIVE_MODULE_PACKAGE_LIMIT (1024u * 1024u)
#define FX_NATIVE_MODULE_IMAGE_LIMIT (4u * 1024u * 1024u)
#define FX_ISA_AARCH64 1u
#define FX_MODULE_STACK_DEFAULT 65536u
#define FX_MODULE_NATIVE_STACK_MIN 32768u
#define FX_MODULE_USER_STACK_MIN 4096u
#define FX_MODULE_STACK_MAX (1024u * 1024u)
typedef struct {uint32_t native_bytes,user_bytes;} FxModuleStacks;
static inline int fx_module_stacks_valid(FxModuleStacks s) {
    return s.native_bytes>=FX_MODULE_NATIVE_STACK_MIN && s.native_bytes<=FX_MODULE_STACK_MAX &&
        s.user_bytes>=FX_MODULE_USER_STACK_MIN && s.user_bytes<=FX_MODULE_STACK_MAX &&
        !((s.native_bytes|s.user_bytes)&4095u);
}
enum {FX_MODULE_ENTRY_LEGACY=0u,FX_MODULE_ENTRY_SERVICES=1u};
/* v2/v3 declare the copied-service entry ABI at byte116. v3 declares native
 * and EL0 stack bytes at120/124; v1/v2 retain the original 64KiB defaults.
 * v1 SDK packages can
 * be recognized by their original service capability bits, never filename.
 * Zero-capability compute-only SDK modules must use the explicit v2 tag. */
static inline uint32_t fx_module_header_entry(const uint8_t *p,uint32_t bytes) {
    if(!p || bytes<FX_MODULE_HEADER)return UINT32_MAX;
    uint32_t version=(uint32_t)p[8]|(uint32_t)p[9]<<8|(uint32_t)p[10]<<16|(uint32_t)p[11]<<24;
    if(version!=1u && version!=2u && version!=3u)return UINT32_MAX;
    if(version<3u)for(uint32_t i=version==2u?120u:116u;i<FX_MODULE_HEADER;++i)if(p[i])return UINT32_MAX;
    if(version>=2u) {
        uint32_t kind=(uint32_t)p[116]|(uint32_t)p[117]<<8|(uint32_t)p[118]<<16|(uint32_t)p[119]<<24;
        return kind==FX_MODULE_ENTRY_SERVICES?kind:UINT32_MAX;
    }
    return ((uint32_t)p[110]|(uint32_t)p[111]<<8)&0x3fu?FX_MODULE_ENTRY_SERVICES:FX_MODULE_ENTRY_LEGACY;
}
enum { FX_READ = 1u, FX_WRITE = 2u, FX_EXECUTE = 4u };

typedef struct {
    uint32_t image_offset, file_offset, file_bytes, memory_bytes, flags, alignment;
} FxSegment;

typedef struct {
    uint32_t abi, isa, bytes, entry_offset, segment_count, relocation_count;
    uint64_t required_capabilities;
    FxSegment segments[FX_MODULE_SEGMENTS];
    uint32_t prepared;
    uint32_t entry_abi;
    FxModuleStacks stacks;
} FxModule;

/* Read-only package preflight against a page-aligned prospective image.
 * Returns the same descriptor/relocation metadata with prepared=0. Callers
 * must independently bind the full package digest before later execution. */
FxResult fx_module_inspect(const uint8_t *,uint32_t package_bytes,const uint8_t firmware[32],
                           uint64_t verified_capabilities,uint32_t image_capacity,FxModule *out);

/* Validates/copies/relocates into private memory. Does not create executable
 * mappings or invoke the entry. Caller must supply the independently verified
 * package digest, target identity and verified capability set. */
FxResult fx_module_prepare(const uint8_t *package, uint32_t package_bytes,
                          const uint8_t expected_package_sha256[32],
                          const uint8_t expected_firmware_sha256[32],
                          uint64_t verified_capabilities, uint8_t *image,
                          uint32_t capacity, FxModule *out);
#endif
