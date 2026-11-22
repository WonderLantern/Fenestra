#ifndef FENESTRA_TIME_H
#define FENESTRA_TIME_H
#include "fenestra.h"
/* Exact integer conversion with truncation toward zero, without multiplying
 * a large counter by the target frequency first. */
static inline FxResult fx_time_scale(uint64_t counter, uint32_t frequency, uint32_t target, uint64_t *out) {
    if (!frequency || !target || !out)
        return FX_ARGUMENT;
    uint64_t whole = counter / frequency, remainder = counter % frequency;
    if (whole > UINT64_MAX / target)
        return FX_BOUNDS;
    uint64_t fraction = remainder * target / frequency;
    whole *= target;
    if (fraction > UINT64_MAX - whole)
        return FX_BOUNDS;
    *out = whole + fraction;
    return FX_OK;
}
#endif
