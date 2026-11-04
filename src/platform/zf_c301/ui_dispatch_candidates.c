#if !defined(FX_ZF_EXPERIMENTAL_UI_DISPATCH) || FX_ZF_EXPERIMENTAL_UI_DISPATCH != 1
#error "Zf UI dispatch candidates are experimental and are not enabled by a production builder"
#endif

#include "ui_dispatch_candidates.h"

static const uint8_t candidate_a_original[] = {
#include "fx_zf_ui_candidate_a.inc"
};
static const uint8_t candidate_b_original[] = {
#include "fx_zf_ui_candidate_b.inc"
};

_Static_assert(sizeof(candidate_a_original) == 688u, "Zf UI candidate A extent");
_Static_assert(sizeof(candidate_b_original) == 356u, "Zf UI candidate B extent");

const FxZfUiDispatchCandidate fx_zf_ui_dispatch_candidates[2] = {
    {0x403e5714u, 0x403e59c4u, 0x403e5910u, 0x403e5920u,
     0x80u, 10000u, sizeof(candidate_a_original), candidate_a_original, 0u, 0u},
    {0x403e6310u, 0x403e6474u, 0x403e63acu, 0x403e63bcu,
     0x80u, 10000u, sizeof(candidate_b_original), candidate_b_original, 0u, 0u},
};

const uint32_t fx_zf_ui_dispatch_candidate_count = 2u;
