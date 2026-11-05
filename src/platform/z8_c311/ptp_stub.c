/* Z8-C311 v0: PTP capability unavailable — profile present for type
 * completeness; every operation reports unsupported. Pinned when the Z8
 * PTP sites are independently verified. */
#include "ptp.h"
#include "fenestra_ptp.h"
#include <stdint.h>
const FxNativePtpProfile fx_native_ptp_profile={0};
/* Enabled-image callbacks reference these until the Z8 PTP sites are
 * pinned; the USB-debug row stays hidden (never active) and the toggle
 * is unreachable, so the stubs only keep the link closed. */
void fx_ptp_enable(FxPtp *p,int enable){(void)p;(void)enable;}
