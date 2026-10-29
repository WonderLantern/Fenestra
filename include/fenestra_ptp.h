#ifndef FENESTRA_PTP_H
#define FENESTRA_PTP_H
#include <stdint.h>
#include <stdatomic.h>

/* Normal vendor operations; no maintenance mode is involved. */
#define FX_PTP_HELLO 0x95e0u
#define FX_PTP_SUBMIT 0x95e1u
#define FX_PTP_FETCH 0x95e2u
#define FX_PTP_CONTROL 0x95e3u
#define FX_PTP_CONTROL_BYTES 32u
#define FX_PTP_FLAG_CONTROL 4u
#define FX_PTP_HEADER 32u
#define FX_PTP_PAYLOAD 1024u
#define FX_PTP_FRAME (FX_PTP_HEADER+FX_PTP_PAYLOAD)
#define FX_PTP_VERSION 2u
enum { FX_PTP_OK, FX_PTP_ARGUMENT, FX_PTP_CLOSED, FX_PTP_STALE,
       FX_PTP_BUSY, FX_PTP_UNSUPPORTED, FX_PTP_INTEGRITY };
enum { FX_PTP_STATUS=1, FX_PTP_ECHO=2, FX_PTP_DIAGNOSTICS=3,
       FX_PTP_REGIONS=4, FX_PTP_READ_REGION=5, FX_PTP_LINUX_BRIDGE=6, FX_PTP_RTOS_MONITOR=7,
       FX_PTP_OBSERVATION=8, FX_PTP_IDENTITY=9 };
#define FX_PTP_BASE_CAPABILITIES 255u
#define FX_PTP_CAPABILITIES 511u
#define FX_PTP_IDENTITY_BYTES 256u
enum { FX_PTP_IDLE, FX_PTP_QUEUED, FX_PTP_RUNNING, FX_PTP_DONE,
       FX_PTP_CANCELLED, FX_PTP_RELEASED };
enum { FX_PTP_JOB_STATUS, FX_PTP_JOB_CANCEL, FX_PTP_JOB_RELEASE };

/* Result bytes belong to the worker until publication. Neither USB nor a menu
 * callback executes a backend. One retained job gives bounded retry semantics. */
typedef uint32_t (*FxPtpBackend)(void *,uint16_t,const uint8_t *,uint32_t,
                               uint8_t *,uint32_t *);
/* Immutable native-profile storage, retained for the lifetime of the endpoint.
 * No camera name, firmware number or build hash is inferred by the transport.
 * flags bit0=privileged debugging, bit1=source/configuration build identity,
 * bit2=out-of-band job control (also advertised in HELLO flags at byte36). */
typedef struct {
    uint32_t firmware,capabilities,flags;
    const char *model,*revision;
    const uint8_t *application,*module_target,*build;
} FxPtpIdentity;
typedef struct {
    _Atomic uint32_t enabled,generation,locked,completed;
    uint32_t observed_generation,session_sequence,job,request_id,request_bytes;
    uint64_t session;
    uint32_t response_bytes,executions,rejected,discarded;
    uint8_t request[FX_PTP_FRAME],response[FX_PTP_FRAME];
    uint32_t completion_generation,completion_bytes;
    uint8_t completion[FX_PTP_FRAME];
    _Atomic uint64_t backend_session;
    const FxPtpIdentity *identity;
    uint32_t cancel_requested; /* Under locked; RUNNING storage remains owned. */
} FxPtp;

void fx_ptp_init(FxPtp *);
void fx_ptp_init_target(FxPtp *,const FxPtpIdentity *);
uint32_t fx_ptp_identity(const FxPtp *,const uint8_t *,uint32_t,uint8_t *,uint32_t *);
void fx_ptp_enable(FxPtp *,int);
void fx_ptp_disconnect(FxPtp *);
uint32_t fx_ptp_hello(FxPtp *,uint8_t out[64]);
uint32_t fx_ptp_submit(FxPtp *,const uint8_t *,uint32_t);
uint32_t fx_ptp_fetch(FxPtp *,uint64_t,uint32_t,uint32_t,uint8_t *,uint32_t,uint32_t *);
uint32_t fx_ptp_poll(FxPtp *,FxPtpBackend,void *);
uint32_t fx_ptp_control(FxPtp *,uint64_t,uint32_t,uint32_t,uint8_t out[FX_PTP_CONTROL_BYTES]);
uint32_t fx_ptp_crc(const uint8_t *,uint32_t);
uint32_t fx_ptp_get32(const uint8_t *);
uint64_t fx_ptp_get64(const uint8_t *);
void fx_ptp_put32(uint8_t *,uint32_t);
void fx_ptp_put64(uint8_t *,uint64_t);
/* Copy a standard PTP AUINT16 OperationsSupported array and append any
 * missing Fenestra vendor operations. Bounds failure never writes output. */
uint32_t fx_ptp_extend_operations(uint8_t *,uint32_t,const uint8_t *,uint32_t);

/* Native platform entry points; state is owned by the menu heap allocation. */
FxPtp *fx_c110_menu_ptp(void);
void fx_c110_ptp_init(FxPtp *);
int fx_c110_menu_ptp_active(void);
struct FxC110Demo;
void fx_c110_ptp_poll(struct FxC110Demo *);
uint32_t fx_c110_rtos_debug(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
#endif
