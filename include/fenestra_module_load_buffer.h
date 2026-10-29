#ifndef FENESTRA_MODULE_LOAD_BUFFER_H
#define FENESTRA_MODULE_LOAD_BUFFER_H
#include "fenestra_file_card.h"
#include "fenestra_module_index.h"
#include "fenestra_accounting.h"

/* Core-owned synchronous load scratch, separate from the retained execution
 * allocation. Never mapped into the module view or published as a debug region.
 * Original-table worker calls only. No reset/reuse after uncertain IO cleanup. */
typedef struct {
    FxIndexMemory memory;
    uint8_t *data;
    uint32_t bytes,peak;
    uint64_t released_bytes;
    uint32_t releases,retained;
    FxAccountRow *account; /* Optional trusted owner row, bound before opening. */
} FxModuleLoadBuffer;
FxResult fx_module_load_buffer_account(FxModuleLoadBuffer *,FxAccountRow *);
FxResult fx_module_load_buffer_open(FxModuleLoadBuffer *,FxIndexMemory,uint32_t package_bytes);
/* Invalidates the snapshot before freeing. Error/closed/retained receipts stay
 * available for diagnostics. IO errors with successful synchronous cleanup can
 * release; open/uncertain close/foreign snapshot retain and prohibit reuse. */
FxResult fx_module_load_buffer_finish(FxModuleLoadBuffer *,FxFileCard *,
                                     const FxCardReceipt *,FxResult native_result);
#endif
