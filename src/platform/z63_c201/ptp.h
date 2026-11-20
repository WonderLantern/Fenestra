#ifndef FENESTRA_Z63_PTP_H
#define FENESTRA_Z63_PTP_H
#include "fenestra_native_ptp.h"
FxPtp *fx_z63_menu_ptp(void);
int fx_z63_menu_ptp_active(void);
void fx_z63_ptp_init(FxPtp *);
void fx_z63_ptp_poll(void);
#endif
