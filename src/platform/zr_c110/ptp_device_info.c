/* Profile-specific entry preserves the independently verified original hook. */
#include "fenestra_native_ptp.h"
uint32_t fx_c110_ptp_operations(uint8_t *out,uint32_t capacity,const uint8_t *operations) {
    return fx_native_ptp_operations(out,capacity,operations);
}
