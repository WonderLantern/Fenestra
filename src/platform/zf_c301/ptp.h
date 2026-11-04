#ifndef FENESTRA_ZF_C301_PTP_H
#define FENESTRA_ZF_C301_PTP_H

#include "fenestra.h"
#include "fenestra_native_ptp.h"

/* Experimental owner_native.c supplies the retained endpoint. The admission
 * implementation has explicit core activation and local consent APIs; their
 * actual startup/menu call sites still need integration and qualification.
 * Policy permission is a target-specific contract: it must not be inferred
 * from DeviceInfo publication or implemented by removing stock filters. The
 * owner must disable and revoke an active endpoint when either gate closes,
 * increment the owner generation when policy closes, serialize gate/session
 * changes with buffer/execute, and outlive callbacks and in-flight work.
 * See ptp_admission.h; USB hooks and local menu remain unqualified for Zf. */
FxPtp *fx_zf_ptp_owner(void);
/* Initialize the retained owner endpoint and bind the pinned Zf identity. */
FxResult fx_zf_ptp_init(FxPtp *,FxPtpIdentity *);
FxResult fx_zf_ptp_bind_owner(void);
/* Mutable target authorization belongs to the retained owner, never RX code.
 * Native preflight/execute/session callbacks still require serialization. */
typedef struct {
    _Atomic uintptr_t request;
    _Atomic uint32_t generation;
} FxZfPtpAuthorization;
FxZfPtpAuthorization *fx_zf_ptp_authorization(void);
int fx_zf_ptp_user_enabled(void);
int fx_zf_ptp_policy_permits(void);

const FxNativePtpDescriptor *fx_zf_ptp_lookup(uint32_t);
void fx_zf_ptp_session(uint32_t);
uint32_t fx_zf_ptp_operations(uint8_t *, uint32_t, const uint8_t *);

const FxNativePtpDescriptor *fx_zf_ptp_original_lookup(uint32_t);
void fx_zf_ptp_original_session(uint32_t);
uint32_t fx_zf_ptp_original_operations(uint8_t *, uint32_t, const uint8_t *);
#endif
