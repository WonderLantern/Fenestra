/* Z8-C311 v0: PTP capability unavailable — profile present for type
 * completeness; every operation reports unsupported. Pinned when the Z8
 * PTP sites are independently verified. */
#include "ptp.h"
#include "fenestra_ptp.h"
#include <stdint.h>
const FxNativePtpProfile fx_native_ptp_profile={0};
/* Enabled-image callbacks reference these until the Z8 PTP sites are
 * pinned. On the enabled image the constructed USB-debug row
 * (FX_Z8_CORE_USB_ID) becomes reachable after activation and usb_toggle
 * runs; the stub keeps fx_ptp_enable a safe no-op (no Z8 PTP transport
 * exists to toggle). */
void fx_ptp_enable(FxPtp *p,int enable){(void)p;(void)enable;}
