#ifndef FENESTRA_OBSERVATION_H
#define FENESTRA_OBSERVATION_H
#include <stdint.h>
#include <stdatomic.h>
#define FX_OBS_BYTES 196608u
#define FX_OBS_RECORDS 64u
#define FX_OBS_JOBS 128u
#define FX_OBS_SEGMENTS 10u
#define FX_OBS_RECORD_BYTES 2154u
#define FX_OBS_STATUS_BYTES 192u
enum { FX_OBS_IDLE,FX_OBS_ARMED,FX_OBS_RUNNING,FX_OBS_STOPPING,FX_OBS_COMPLETE,
       FX_OBS_EXPIRED,FX_OBS_CANCELLED,FX_OBS_FAULT };
enum { FX_OBS_LIMIT_COUNT=1,FX_OBS_LIMIT_TIME,FX_OBS_HOST_STOP,FX_OBS_LOCAL_REVOKE,
       FX_OBS_DISCONNECTED,FX_OBS_SOURCE_ERROR,FX_OBS_LIMIT_BYTES };
enum { FX_OBS_DESCRIBE,FX_OBS_SNAPSHOT,FX_OBS_ARM,FX_OBS_STATUS,FX_OBS_READ,
       FX_OBS_STOP,FX_OBS_RELEASE,FX_OBS_FIND,FX_OBS_TRACE,FX_OBS_FRAME };
#define FX_OBS_ALLOW_DISCONNECT 1u
typedef struct {
    void *context;
    uint64_t (*clock)(void *);
    int (*read)(void *,uint64_t,uint32_t,uint8_t *,uint64_t *);
    uint64_t frequency,boot,activation,connection;
    uint8_t build[32];
} FxObservationIO;
typedef struct {
    _Atomic uint32_t lock,stop;
    uint32_t phase,reason,error,period_ms,duration_ms,maximum,flags,records,used,jobs;
    uint64_t id,boot,activation,frequency,start,end,next,deadline,connection;
    uint8_t nonce[16],digest[32],nonces[FX_OBS_JOBS][16];
    uint8_t data[FX_OBS_BYTES];
} FxObservation;
void fx_observation_init(FxObservation *);
void fx_observation_cancel(FxObservation *,uint32_t);
void fx_observation_step(FxObservation *,const FxObservationIO *,int);
int fx_observation_quiesced(FxObservation *);
uint32_t fx_observation_request(FxObservation *,const FxObservationIO *,
    const uint8_t *,uint32_t,uint8_t *,uint32_t *);
#endif
