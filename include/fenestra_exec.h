#ifndef FENESTRA_EXEC_H
#define FENESTRA_EXEC_H
#include "fenestra_module.h"

#define FX_EXEC_PAGE 4096u
#define FX_EXEC_PAGES 1024u
#define FX_EXEC_LEAF_TABLES 3u
#define FX_EXEC_PA_MASK UINT64_C(0x0000fffffffff000)
#define FX_EXEC_PXN (UINT64_C(1) << 53)
#define FX_EXEC_UXN (UINT64_C(1) << 54)
#define FX_EXEC_RW_XN (UINT64_C(3) | (UINT64_C(3) << 8) | (UINT64_C(1) << 10) | FX_EXEC_PXN | FX_EXEC_UXN)

/* Single owner of a private arena, with identity-readable WBWA page tables.
 * This is a constrained EL1 backend, not a Nikon allocator/permission bypass.
 * It refuses aliases, block mappings, unknown table ownership/context and
 * unavailable physical-table mappings. The owner must serialize all table
 * mutation and keep all other users out of this arena during publication. */
typedef struct {
    uint64_t virtual_base, physical_base, ttbr0, tcr, mair;
    volatile uint64_t *leaf;
    uint32_t capacity, pages, bound, published;
    uint64_t *expected,*staging;
    volatile uint64_t *leaf_tables[FX_EXEC_LEAF_TABLES];
    uint32_t leaf_table_count,storage_pages;
} FxExecArena;

FxResult fx_exec_plan(const FxModule *, uint64_t virtual_base, uint64_t physical_base,
                      uint32_t capacity, uint64_t *descriptors,uint32_t descriptor_capacity);
/* Two owner-provided, nonoverlapping arrays, sized to the actual image pages.
 * They remain private and live with the arena; never supply image/table RAM. */
typedef struct {uint64_t *expected,*staging;uint32_t pages;} FxExecStorage;
FxResult fx_exec_bind(FxExecArena *, uint64_t virtual_base, uint64_t physical_base,
                      uint32_t capacity, volatile uint64_t *first_owned_leaf,FxExecStorage);
/* Relative allocation layout; no heap operation or native address is guessed.
 * Worst-case table count includes unaligned L3 and 1 GiB crossings. Caller adds
 * one page of alignment slack to bytes, then owns the entire resulting scope. */
typedef struct {
    uint32_t bytes,tables,private_tables,user_tables,native_stack,api,exchange,extra;
    /* package/file are zero: load scratch has a separate temporary allocation. */
    uint32_t user_stack,package,file,expected,staging,image,image_bytes,package_bytes;
    uint32_t native_stack_bytes,user_stack_bytes;
} FxExecLayout;
FxResult fx_exec_layout_stacks(uint32_t metadata_bytes,uint32_t package_bytes,uint32_t image_bytes,
                        uint32_t exchange_bytes,uint32_t extra_bytes,FxModuleStacks,FxExecLayout *);
FxResult fx_exec_layout(uint32_t metadata_bytes,uint32_t package_bytes,uint32_t image_bytes,
                        uint32_t exchange_bytes,uint32_t extra_bytes,FxExecLayout *);
FxResult fx_exec_publish(FxExecArena *, const FxModule *);
FxResult fx_exec_live(const FxExecArena *);
/* Call only after module admission closes, all leases leave and the platform
 * has stopped every arena/DMA user. It only removes this arena's mappings. */
FxResult fx_exec_revoke(FxExecArena *);
#endif
