#ifndef FENESTRA_MMU_CONTEXT_H
#define FENESTRA_MMU_CONTEXT_H
#include <stdint.h>
#include <stddef.h>
/* Independently decoded from both native kernels. A view is only a function
 * argument. Never install it as a task/MMU root or publish its stack address.
 * All native writers of the source/pool must be externally serialized. */
typedef struct {
    uintptr_t root,pool;
    uint32_t pages;
    uint8_t allocated[20];
    uint64_t physical_root,physical_pool;
    uint8_t private_roots,reserved[7];
    uintptr_t roots[3];
} FxNativeMmuContext;
/* The live stock pool can advertise 192 pages. Original allocators scan
 * bytes at context+0x14+i/8 up to pages: indices >=160 overlap physical_root.
 * Never reject a usable larger stock pool, or copy that overrun into another
 * context field. A temporary view allocates only in the verified 20-byte
 * bitmap; full lower capacity returns the native allocation error. Existing
 * descriptors outside that allocator subset remain readable/translatable. */
static inline uint32_t fx_native_mmu_view_pages(const FxNativeMmuContext *source) {
    uint32_t capacity=(uint32_t)sizeof(source->allocated)*8u;
    return source->pages<capacity?source->pages:capacity;
}
_Static_assert(sizeof(FxNativeMmuContext)==0x58,"native MMU context size");
_Static_assert(offsetof(FxNativeMmuContext,allocated)==0x14 &&
               offsetof(FxNativeMmuContext,private_roots)==0x38 &&
               offsetof(FxNativeMmuContext,roots)==0x40,"native MMU context fields");
#endif
