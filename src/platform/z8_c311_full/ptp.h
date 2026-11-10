/* Z8 port derived from upstream src/platform/z9_c532/ptp.h.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#ifndef FENESTRA_Z8_PTP_H
#define FENESTRA_Z8_PTP_H
#include "fenestra_native_ptp.h"
FxPtp *fx_z8_menu_ptp(void);
int fx_z8_menu_ptp_active(void);
void fx_z8_ptp_init(FxPtp *);
void fx_z8_ptp_poll(void);
#endif
