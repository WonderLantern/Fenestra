#ifndef FENESTRA_ZF_C301_UI_DISPATCH_CANDIDATES_H
#define FENESTRA_ZF_C301_UI_DISPATCH_CANDIDATES_H

#include <stdint.h>

/* Synchronous candidate callees. wait_wrapper/set_wrapper are original BL
 * sites, not callable function addresses or qualified main-loop hooks. */
typedef struct {
    uintptr_t start, end, wait_wrapper, set_wrapper;
    uint32_t mask, timeout, bytes;
    const uint8_t *original;
    uint8_t complete_function_fde, ui_owner_verified;
} FxZfUiDispatchCandidate;

extern const FxZfUiDispatchCandidate fx_zf_ui_dispatch_candidates[2];
extern const uint32_t fx_zf_ui_dispatch_candidate_count;

#endif
