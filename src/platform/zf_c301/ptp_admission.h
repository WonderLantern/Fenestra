#ifndef FX_ZF_C301_PTP_ADMISSION_H
#define FX_ZF_C301_PTP_ADMISSION_H
#include "fenestra_platform.h"
#include "ptp.h"
#include "lifecycle.h"

/* Core-owned retained state, zero before publication. Identity and its pointed
 * bytes are immutable until reboot. No menu choice or native filter is
 * inferred from this record. Backends match the original Z9 transport. */
#define FX_ZF_PTP_CAPS ((1u<<(FX_PTP_STATUS-1u))|(1u<<(FX_PTP_ECHO-1u))|(1u<<(FX_PTP_IDENTITY-1u))|(1u<<(FX_PTP_RTOS_MONITOR-1u))|(1u<<(FX_PTP_LINUX_BRIDGE-1u)))
#define FX_ZF_PTP_FLAGS (1u|FX_PTP_FLAG_CONTROL)
typedef struct {
    _Atomic uint32_t locked,bound,consent,activated;
} FxZfPtpAdmission;
FxZfPtpAdmission *fx_zf_ptp_admission(void);

/* Trusted core only. Bind once, before polling/USB hooks. No reinitialization
 * of FxPtp or caller-supplied native address/backend. Consent defaults OFF.
 * Activation belongs to core startup; consent belongs to a local UI action.
 * Neither is inferred from USB traffic. BUSY means caller must retry. */
FxResult fx_zf_ptp_bind(const FxPtpIdentity *);
FxResult fx_zf_ptp_set_activated(int);
FxResult fx_zf_ptp_set_user_enabled(int);
/* Native callback exclusion: a failed try-enter never owns the lock. State
 * callbacks called inside an admitted descriptor only read atomic gates. */
int fx_zf_ptp_enter(void);
void fx_zf_ptp_leave(void);
/* Worker context only. Backends never execute in the USB callback. */
uint32_t fx_zf_ptp_poll(void);
#endif
