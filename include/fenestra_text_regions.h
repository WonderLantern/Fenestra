#ifndef FENESTRA_TEXT_REGIONS_H
#define FENESTRA_TEXT_REGIONS_H
#include "fenestra.h"
#include <stdatomic.h>

/* Core-only retained allocation ownership, not a module address validator.
 * Creators publish intrusive nodes through a nonblocking writer gate; BUSY
 * retains the caller's node for retry. Readers never lock, allocate or
 * dereference the candidate text address. No deletion/reset until reboot. */
typedef struct {
    _Atomic uintptr_t child[2];
    uintptr_t prefix;
    uint32_t bit;
} FxTextRegionBranch;
typedef struct {
    uintptr_t begin,end;
    FxTextRegionBranch branch;
    _Atomic uintptr_t domain;
} FxTextRegion;
typedef struct {
    _Atomic uintptr_t root;
    _Atomic uint32_t writer,count;
} FxTextRegions;
FxResult fx_text_regions_add(FxTextRegions *,FxTextRegion *,const void *,uintptr_t bytes);
/* Published RTOS indices use this EL1 facade: save/restore local IRQ/FIQ
 * around the bounded metadata-only writer. It cannot wait behind a task
 * preempted on the same CPU. No kernel/provider/allocation call under it.
 * Hosted builds keep the nonblocking primitive for explicit test fixtures. */
FxResult fx_native_text_regions_add(FxTextRegions *,FxTextRegion *,const void *,uintptr_t bytes);
/* Optional visits counts tree nodes, not original font work or CPU cycles. */
int fx_text_regions_contains(const FxTextRegions *,uintptr_t address,uintptr_t bytes,uint32_t *visits);
#endif
