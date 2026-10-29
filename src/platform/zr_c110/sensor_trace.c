/* Bounded passive sensor software-submit recorder. No hardware commands,
 * MMIO, allocation, waits, native calls or hashing in submit/return callbacks.
 * The worker freezes/hash-publishes only after every accepted call returns. */
#include "sensor_trace.h"
#include "fenestra_ptp.h"
#include "fenestra_memory.h"
#include "fenestra_sha256.h"
#define TX_BUFFER UINT64_C(0xa057af9ec)
#define CALL_SITE UINT64_C(0x413522a8)
#define RETURN_SITE UINT64_C(0x413522ac)
static int lock(FxSensorTrace *s) {
    uint32_t zero=0;return atomic_compare_exchange_strong_explicit(&s->lock,&zero,1u,memory_order_acquire,memory_order_relaxed);
}
static void unlock(FxSensorTrace *s){atomic_store_explicit(&s->lock,0u,memory_order_release);}
void fx_sensor_trace_init(FxSensorTrace *s){memset(s,0,sizeof(*s));}
void fx_sensor_trace_cancel(FxSensorTrace *s,uint32_t reason) {
    if(!s)return;
    atomic_store_explicit(&s->accepting,0u,memory_order_release);
    uint32_t zero=0;(void)atomic_compare_exchange_strong_explicit(&s->stop,&zero,reason,memory_order_release,memory_order_relaxed);
}
static int authorized(FxSensorTrace *s,const FxObservationIO *io,int admitted) {
    uint32_t reason=0;
    if(!admitted || s->boot!=io->boot || s->activation!=io->activation)reason=FX_OBS_LOCAL_REVOKE;
    else if(s->frequency!=io->frequency){reason=FX_OBS_SOURCE_ERROR;s->error=FX_PTP_INTEGRITY;}
    else if(!(s->flags&FX_OBS_ALLOW_DISCONNECT) && s->connection!=io->connection)reason=FX_OBS_DISCONNECTED;
    else if(io->clock(io->context)>=s->deadline)reason=FX_OBS_LIMIT_TIME;
    if(reason)fx_sensor_trace_cancel(s,reason);
    return !reason && !atomic_load_explicit(&s->stop,memory_order_acquire);
}
static void header(uint8_t *r,const FxSensorTrace *s,const FxObservationIO *io,
                   const FxSensorTraceFrame *f,uint64_t token,uint32_t sequence,uint32_t kind) {
    memset(r,0,FX_SENSOR_TRACE_RECORD);memcpy(r,"FXTR",4u);
    fx_ptp_put32(r+4,1u);fx_ptp_put32(r+8,FX_SENSOR_TRACE_RECORD);fx_ptp_put32(r+12,sequence);
    fx_ptp_put64(r+16,token); /* Same real invocation; parent token is UNKNOWN/0. */
    fx_ptp_put64(r+32,io->clock(io->context));fx_ptp_put64(r+40,s->frequency);
    fx_ptp_put64(r+48,s->id);fx_ptp_put64(r+56,s->boot);fx_ptp_put64(r+64,s->activation);
    fx_ptp_put64(r+72,kind==1u?CALL_SITE:RETURN_SITE);fx_ptp_put64(r+80,(uintptr_t)f+FX_SENSOR_TRACE_FRAME);
    fx_ptp_put64(r+88,f->tpidrro);fx_ptp_put64(r+96,f->mpidr);
    for(uint32_t i=0;i<4u;++i)fx_ptp_put64(r+104u+i*8u,f->x[i]);
    fx_ptp_put32(r+136,kind);fx_ptp_put32(r+140,8u); /* Fixed path's sensor transport selector, not dynamic handle. */
    fx_ptp_put32(r+144,FX_ST_SOFTWARE_ONLY);fx_ptp_put32(r+156,16u); /* actual u16 LE TX words */
    memcpy(r+224,io->build,32u);
}
uint64_t fx_sensor_trace_enter(FxSensorTrace *s,const FxObservationIO *io,const FxSensorTraceFrame *f,int admitted) {
    if(!s || !atomic_load_explicit(&s->accepting,memory_order_acquire))return 0;
    atomic_fetch_add_explicit(&s->callbacks,1u,memory_order_acquire);
    if(!atomic_load_explicit(&s->accepting,memory_order_acquire)) {
        atomic_fetch_sub_explicit(&s->callbacks,1u,memory_order_release);return 0;
    }
    if(!lock(s)) {
        atomic_fetch_add_explicit(&s->dropped,1u,memory_order_relaxed);
        atomic_fetch_sub_explicit(&s->callbacks,1u,memory_order_release);return 0;
    }
    if(!authorized(s,io,admitted) || s->pairs>=s->maximum) {
        if(s->pairs>=s->maximum)fx_sensor_trace_cancel(s,FX_OBS_LIMIT_COUNT);
        unlock(s);atomic_fetch_sub_explicit(&s->callbacks,1u,memory_order_release);return 0;
    }
    uint32_t pair=s->pairs++;uint64_t token=(uint64_t)s->generation<<32 | (uint64_t)(pair+1u);
    atomic_store_explicit(&s->returned[pair],0u,memory_order_relaxed);
    atomic_fetch_add_explicit(&s->inflight,1u,memory_order_relaxed);
    uint8_t *r=s->data+(size_t)pair*2u*FX_SENSOR_TRACE_RECORD;
    header(r,s,io,f,token,pair*2u,1u);memset(r+FX_SENSOR_TRACE_RECORD,0,FX_SENSOR_TRACE_RECORD);
    /* The exact descriptor submitted at the original BL. The wrapper toggles
     * GPIO/CS, not a proven mutex: this software copy is NOT source-atomic. */
    uint8_t descriptor[24];uint64_t par=1u;uint32_t copied=0,total=0,flags=FX_ST_SOFTWARE_ONLY;
    uint64_t stack=(uintptr_t)f+FX_SENSOR_TRACE_FRAME;
    if(f->x[1]!=stack || f->x[29]>192u || !f->x[29] ||
       !io->read(io->context,stack,sizeof(descriptor),descriptor,&par) ||
       fx_ptp_get64(descriptor)!=TX_BUFFER || fx_ptp_get64(descriptor+8)!=0 ||
       fx_ptp_get32(descriptor+16)!=f->x[29] || fx_ptp_get32(descriptor+20)!=f->x[29])flags|=FX_ST_SOURCE_ERROR;
    else {
        total=(uint32_t)f->x[29]*2u;copied=total<64u?total:64u;
        if(!io->read(io->context,TX_BUFFER,copied,r+160u,&par)){copied=0;memset(r+160u,0,64u);flags|=FX_ST_SOURCE_ERROR;}
        if(total>copied)flags|=FX_ST_TRUNCATED;
    }
    fx_ptp_put32(r+144,flags);fx_ptp_put32(r+148,total);fx_ptp_put32(r+152,copied);
    s->phase=FX_OBS_RUNNING;
    if(flags&FX_ST_SOURCE_ERROR){s->error=FX_PTP_INTEGRITY;fx_sensor_trace_cancel(s,FX_OBS_SOURCE_ERROR);}
    else if(s->pairs>=s->maximum)fx_sensor_trace_cancel(s,FX_OBS_LIMIT_COUNT);
    unlock(s);atomic_fetch_sub_explicit(&s->callbacks,1u,memory_order_release);return token;
}
void fx_sensor_trace_leave(FxSensorTrace *s,const FxObservationIO *io,const FxSensorTraceFrame *f) {
    if(!s || !f->token)return;
    uint32_t pair=(uint32_t)f->token;
    if(!pair || pair>FX_SENSOR_TRACE_PAIRS || (uint32_t)(f->token>>32)!=s->generation ||
       pair>s->pairs) {
        atomic_fetch_add_explicit(&s->invalid_return,1u,memory_order_relaxed);return;
    }
    --pair;uint32_t zero=0;
    if(!atomic_compare_exchange_strong_explicit(&s->returned[pair],&zero,1u,memory_order_acquire,memory_order_relaxed)) {
        atomic_fetch_add_explicit(&s->invalid_return,1u,memory_order_relaxed);return;
    }
    uint8_t *r=s->data+((size_t)pair*2u+1u)*FX_SENSOR_TRACE_RECORD;
    header(r,s,io,f,f->token,pair*2u+1u,2u);
    atomic_store_explicit(&s->returned[pair],2u,memory_order_release);
    atomic_fetch_sub_explicit(&s->inflight,1u,memory_order_release);
}
void fx_sensor_trace_step(FxSensorTrace *s,const FxObservationIO *io,int admitted) {
    if(!s || !lock(s))return;
    if(s->phase!=FX_OBS_ARMED && s->phase!=FX_OBS_RUNNING && s->phase!=FX_OBS_STOPPING){unlock(s);return;}
    (void)authorized(s,io,admitted);uint32_t reason=atomic_load_explicit(&s->stop,memory_order_acquire);
    if(!reason){unlock(s);return;}
    s->phase=FX_OBS_STOPPING;s->reason=reason;
    if(atomic_load_explicit(&s->inflight,memory_order_acquire) ||
       atomic_load_explicit(&s->callbacks,memory_order_acquire)){unlock(s);return;}
    for(uint32_t i=0;i<s->pairs;++i)if(atomic_load_explicit(&s->returned[i],memory_order_acquire)!=2u){unlock(s);return;}
    fx_sha256(s->data,s->pairs*2u*FX_SENSOR_TRACE_RECORD,s->digest);s->end=io->clock(io->context);
    s->phase=reason==FX_OBS_SOURCE_ERROR?FX_OBS_FAULT:reason==FX_OBS_LIMIT_TIME?FX_OBS_EXPIRED:
        (reason==FX_OBS_LIMIT_COUNT || reason==FX_OBS_HOST_STOP)?FX_OBS_COMPLETE:FX_OBS_CANCELLED;
    unlock(s);
}
int fx_sensor_trace_quiesced(const FxSensorTrace *s) {
    return !s || (!atomic_load_explicit(&s->accepting,memory_order_acquire) &&
        !atomic_load_explicit(&s->inflight,memory_order_acquire) &&
        !atomic_load_explicit(&s->callbacks,memory_order_acquire));
}
static uint32_t status(FxSensorTrace *s,const FxObservationIO *io,uint8_t *out,uint32_t *n) {
    memset(out,0,192u);memcpy(out,"FXTC",4u);fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,192u);
    fx_ptp_put32(out+12,s->phase);fx_ptp_put64(out+16,s->id);fx_ptp_put64(out+24,s->boot);fx_ptp_put64(out+32,s->activation);
    memcpy(out+40,s->nonce,16u);fx_ptp_put32(out+56,1u);fx_ptp_put32(out+60,s->flags);
    fx_ptp_put32(out+64,s->duration_ms);fx_ptp_put32(out+68,s->maximum);fx_ptp_put32(out+72,s->pairs);
    fx_ptp_put32(out+76,atomic_load_explicit(&s->inflight,memory_order_acquire));
    fx_ptp_put32(out+80,s->pairs*2u*FX_SENSOR_TRACE_RECORD);fx_ptp_put32(out+84,FX_SENSOR_TRACE_BYTES);
    fx_ptp_put32(out+88,s->reason);fx_ptp_put32(out+92,s->error);fx_ptp_put64(out+96,s->start);
    fx_ptp_put64(out+104,s->end);fx_ptp_put64(out+112,s->frequency);
    fx_ptp_put32(out+120,atomic_load_explicit(&s->dropped,memory_order_relaxed));
    fx_ptp_put32(out+124,atomic_load_explicit(&s->invalid_return,memory_order_relaxed));
    memcpy(out+128,s->digest,32u);memcpy(out+160,io->build,32u);*n=192u;return FX_PTP_OK;
}
uint32_t fx_sensor_trace_request(FxSensorTrace *s,const FxObservationIO *io,const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    *n=0;if(!s || !io || bytes<4u)return FX_PTP_ARGUMENT;
    if(!lock(s))return FX_PTP_BUSY;
    uint32_t op=fx_ptp_get32(in),result=FX_PTP_ARGUMENT;
    if(op==FX_ST_ARM && bytes==36u) {
        uint32_t duration=fx_ptp_get32(in+20),maximum=fx_ptp_get32(in+24),flags=fx_ptp_get32(in+28);
        uint8_t nonzero=0;for(uint32_t i=4;i<20u;++i)nonzero|=in[i];
        if(!nonzero || !duration || duration>30000u || !maximum || maximum>FX_SENSOR_TRACE_PAIRS ||
           (flags&~FX_OBS_ALLOW_DISCONNECT) || fx_ptp_get32(in+32) || !io->frequency || !io->boot || !io->activation)goto done;
        for(uint32_t i=0;i<s->jobs;++i)if(!memcmp(s->nonces[i],in+4,16u)) {
            result=!memcmp(s->nonce,in+4,16u) && s->phase!=FX_OBS_IDLE &&
                s->boot==io->boot && s->activation==io->activation?status(s,io,out,n):FX_PTP_STALE;goto done;
        }
        if(s->phase!=FX_OBS_IDLE || s->jobs>=FX_OBS_JOBS){result=FX_PTP_BUSY;goto done;}
        uint64_t now=io->clock(io->context);
        if(io->frequency>UINT64_MAX/30000u || !(io->frequency*duration/1000u) ||
           now>UINT64_MAX-io->frequency*30000u/1000u)goto done;
        memcpy(s->nonce,in+4,16u);memcpy(s->nonces[s->jobs++],in+4,16u);
        ++s->generation;s->duration_ms=duration;s->maximum=maximum;s->flags=flags;
        s->boot=io->boot;s->activation=io->activation;s->connection=io->connection;s->frequency=io->frequency;
        s->start=now;s->end=0;s->deadline=now+io->frequency*duration/1000u;
        uint8_t identity[72],digest[32];memcpy(identity,s->nonce,16u);memcpy(identity+16,io->build,32u);
        fx_ptp_put64(identity+48,s->boot);fx_ptp_put64(identity+56,s->activation);fx_ptp_put64(identity+64,s->generation);
        fx_sha256(identity,sizeof(identity),digest);s->id=fx_ptp_get64(digest);if(!s->id)s->id=1;
        s->pairs=s->reason=s->error=0;memset(s->digest,0,sizeof(s->digest));
        atomic_store(&s->stop,0u);atomic_store(&s->inflight,0u);atomic_store(&s->dropped,0u);atomic_store(&s->invalid_return,0u);
        s->phase=FX_OBS_ARMED;atomic_store_explicit(&s->accepting,1u,memory_order_release);
        result=status(s,io,out,n);
    } else if(op==FX_ST_FIND && bytes==20u) {
        result=s->phase!=FX_OBS_IDLE && s->boot==io->boot && s->activation==io->activation &&
            !memcmp(s->nonce,in+4,16u)?status(s,io,out,n):FX_PTP_STALE;
    } else if(bytes>=28u) {
        if(s->phase==FX_OBS_IDLE || fx_ptp_get64(in+4)!=s->id || fx_ptp_get64(in+12)!=s->boot ||
           fx_ptp_get64(in+20)!=s->activation || s->boot!=io->boot || s->activation!=io->activation){result=FX_PTP_STALE;goto done;}
        if(op==FX_ST_STATUS && bytes==28u)result=status(s,io,out,n);
        else if(op==FX_ST_STOP && bytes==28u){fx_sensor_trace_cancel(s,FX_OBS_HOST_STOP);result=status(s,io,out,n);}
        else if(op==FX_ST_RELEASE && bytes==28u) {
            if(s->phase<FX_OBS_COMPLETE || !fx_sensor_trace_quiesced(s))result=FX_PTP_BUSY;
            else {s->phase=FX_OBS_IDLE;result=FX_PTP_OK;}
        } else if(op==FX_ST_READ && bytes==36u) {
            uint32_t offset=fx_ptp_get32(in+28),count=fx_ptp_get32(in+32),used=s->pairs*2u*FX_SENSOR_TRACE_RECORD;
            if(s->phase<FX_OBS_COMPLETE)result=FX_PTP_BUSY;
            else if(count && count<=1008u && offset<=used && count<=used-offset) {
                memcpy(out,s->data+offset,count);*n=count;result=FX_PTP_OK;
            }
        }
    }
done:unlock(s);return result;
}
