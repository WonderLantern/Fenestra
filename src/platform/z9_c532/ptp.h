#ifndef FENESTRA_Z9_PTP_H
#define FENESTRA_Z9_PTP_H
#include "fenestra_native_ptp.h"
FxPtp *fx_z9_menu_ptp(void);
int fx_z9_menu_ptp_active(void);
void fx_z9_ptp_init(FxPtp *);
void fx_z9_ptp_poll(void);
#endif
