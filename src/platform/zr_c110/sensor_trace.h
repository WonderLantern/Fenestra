#ifndef FX_C110_SENSOR_TRACE_H
#define FX_C110_SENSOR_TRACE_H
#include "fenestra_observation.h"
#define FX_SENSOR_TRACE_PAIRS 128u
#define FX_SENSOR_TRACE_RECORD 256u
#define FX_SENSOR_TRACE_BYTES (FX_SENSOR_TRACE_PAIRS*2u*FX_SENSOR_TRACE_RECORD)
#define FX_SENSOR_TRACE_FRAME 320u
enum {FX_ST_DESCRIBE,FX_ST_ARM,FX_ST_STATUS,FX_ST_READ,FX_ST_STOP,FX_ST_RELEASE,FX_ST_FIND};
enum {FX_ST_TRUNCATED=1u,FX_ST_SOURCE_ERROR=2u,FX_ST_SOFTWARE_ONLY=4u};
/* Two fixed sites around the ORIGINAL sensor-only transport call. GP/NZCV
 * preservation is performed by the gate; observers compile integer-only.
 * token/TLS/CPU fields are protected native stack storage, not an EL0 ABI. */
typedef struct {
    uint64_t x[31],nzcv,token,tpidrro,mpidr,reserved[5];
} FxSensorTraceFrame;
_Static_assert(sizeof(FxSensorTraceFrame)==FX_SENSOR_TRACE_FRAME,"native gate frame");
typedef struct {
    _Atomic uint32_t lock,accepting,stop,inflight,dropped,invalid_return,callbacks;
    _Atomic uint32_t returned[FX_SENSOR_TRACE_PAIRS];
    uint32_t phase,reason,error,generation,jobs,maximum,duration_ms,flags;
    _Atomic uint32_t pairs;
    uint64_t id,boot,activation,connection,frequency,start,end,deadline;
    uint8_t nonce[16],nonces[FX_OBS_JOBS][16],digest[32],data[FX_SENSOR_TRACE_BYTES];
} FxSensorTrace;
void fx_sensor_trace_init(FxSensorTrace *);
void fx_sensor_trace_cancel(FxSensorTrace *,uint32_t reason);
uint64_t fx_sensor_trace_enter(FxSensorTrace *,const FxObservationIO *,const FxSensorTraceFrame *,int admitted);
void fx_sensor_trace_leave(FxSensorTrace *,const FxObservationIO *,const FxSensorTraceFrame *);
void fx_sensor_trace_step(FxSensorTrace *,const FxObservationIO *,int admitted);
int fx_sensor_trace_quiesced(const FxSensorTrace *);
uint32_t fx_sensor_trace_request(FxSensorTrace *,const FxObservationIO *,const uint8_t *,uint32_t,uint8_t *,uint32_t *);
#endif
