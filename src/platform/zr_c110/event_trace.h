#ifndef FX_C110_EVENT_TRACE_H
#define FX_C110_EVENT_TRACE_H
#include "fenestra_observation.h"
#include "frame_sample.h"
#define FX_ET_RECORD 512u
#define FX_ET_RECORDS 256u
#define FX_ET_BYTES (FX_ET_RECORD*FX_ET_RECORDS)
#define FX_ET_SCOPES 64u
#define FX_ET_PAYLOAD 192u
#define FX_ET_WAIT_REC_START 2u
enum {FX_ET_ENTRY=1,FX_ET_EXIT,FX_ET_STAGE,FX_ET_TAIL};
enum {FX_ET_SOURCE_ERROR=1u,FX_ET_SCOPE_UNKNOWN=2u,FX_ET_CONTEXT_UNKNOWN=4u,
      FX_ET_GAP=8u,FX_ET_PARENT_UNKNOWN=16u,FX_ET_SOFTWARE_ONLY=32u,FX_ET_ORPHAN=64u,
      FX_ET_REC_START=128u};
typedef struct {
    uint64_t thread,sp,token,parent,args[4];
    uint32_t template_id,association,tail,used;
} FxEventScope;
typedef struct {
    uint32_t template_id,event,kind,flags,bytes,reads;
    uint64_t thread,pc,sp,kernel,cpu,entry_sp,begin,end,registers[16];
    uint8_t payload[FX_ET_PAYLOAD];
} FxEventInput;
typedef struct {
    _Atomic uint32_t lock,accepting,stop,callbacks,dropped,association,rec_started;
    uint32_t phase,reason,error,generation,jobs,maximum,duration_ms,flags,records,scopes,serial,orphans;
    uint64_t id,boot,activation,connection,frequency,start,end,deadline,mask;
    uint8_t nonce[16],nonces[FX_OBS_JOBS][16],digest[32];
    FxEventScope scope[FX_ET_SCOPES];
    uint8_t data[FX_ET_BYTES];
    FxFrameSample frame;
} FxEventTrace;
void fx_event_trace_init(FxEventTrace *);
void fx_event_trace_cancel(FxEventTrace *,uint32_t reason);
int fx_event_trace_begin(FxEventTrace *,uint32_t template_id);
void fx_event_trace_append(FxEventTrace *,const FxObservationIO *,const FxEventInput *,int admitted);
void fx_event_trace_step(FxEventTrace *,const FxObservationIO *,int admitted);
int fx_event_trace_quiesced(const FxEventTrace *);
uint32_t fx_event_trace_request(FxEventTrace *,const FxObservationIO *,uint64_t supported,
    const uint8_t *,uint32_t,uint8_t *,uint32_t *);
#endif
