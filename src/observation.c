/* Fixed read-only configuration recorder. No native calls, sensor transactions,
 * media persistence or PTP-session ownership; every source copy is bounded. */
#include "fenestra_observation.h"
#include "fenestra_ptp.h"
#include "fenestra_memory.h"
#include "fenestra_sha256.h"
static const struct {uint64_t address;uint32_t bytes;} segments[FX_OBS_SEGMENTS]={
    {UINT64_C(0xa02b6f4c8),408},{UINT64_C(0xa057b2250),100},
    {UINT64_C(0xa03df3d8c),4},{UINT64_C(0xa02be8df8),32},
    {UINT64_C(0xa03df2938),192},{UINT64_C(0xa03def068),64},
    {UINT64_C(0xa057b0a20),128},{UINT64_C(0xa057b0eec),18},
    {UINT64_C(0xa02b6f4c8),408},{UINT64_C(0xa03df2938),192}
};
static int lock(FxObservation *s) {
    uint32_t zero=0;return atomic_compare_exchange_strong_explicit(&s->lock,&zero,1u,
        memory_order_acquire,memory_order_relaxed);
}
static void unlock(FxObservation *s) {atomic_store_explicit(&s->lock,0u,memory_order_release);}
static int terminal(uint32_t phase) {return phase>=FX_OBS_COMPLETE;}
void fx_observation_init(FxObservation *s) {memset(s,0,sizeof(*s));}
static uint64_t capture_id(const FxObservation *s,const FxObservationIO *io,const uint8_t nonce[16]) {
    /* Counter boot stamps can repeat after a cold start. Bind the opaque
     * identity to the one-use host nonce as well as its local namespace. */
    uint8_t scope[24],digest[32];FxSha256 hash;
    fx_ptp_put64(scope,io->boot);fx_ptp_put64(scope+8,io->activation);
    fx_ptp_put64(scope+16,(uint64_t)s->jobs+1u);
    fx_sha256_init(&hash);(void)fx_sha256_update(&hash,"FXOBS-ID1",9u);
    (void)fx_sha256_update(&hash,nonce,16u);(void)fx_sha256_update(&hash,scope,sizeof(scope));
    (void)fx_sha256_update(&hash,io->build,32u);fx_sha256_final(&hash,digest);
    uint64_t id=fx_ptp_get64(digest);return id?id:1u;
}
void fx_observation_cancel(FxObservation *s,uint32_t reason) {
    if(!s)return;
    uint32_t zero=0;(void)atomic_compare_exchange_strong_explicit(&s->stop,&zero,reason,
        memory_order_release,memory_order_relaxed);
}
static void finish(FxObservation *s,uint32_t phase,uint32_t reason,uint64_t now) {
    s->phase=FX_OBS_STOPPING;s->reason=reason;s->end=now;
    FxSha256 hash;fx_sha256_init(&hash);
    (void)fx_sha256_update(&hash,s->data,s->used);fx_sha256_final(&hash,s->digest);
    s->phase=phase;
}
static int source_template(const uint8_t *pool,const uint8_t *manager) {
    return fx_ptp_get32(pool)==512u && fx_ptp_get32(pool+4)==0x40c86000u &&
        fx_ptp_get32(pool+8)==0x8a3400u && fx_ptp_get32(pool+12)==120u &&
        fx_ptp_get64(pool+16)==UINT64_C(0x8af5d9c00) &&
        fx_ptp_get32(pool+24)==1u && fx_ptp_get32(pool+28)==7u &&
        fx_ptp_get32(manager)==1u && fx_ptp_get64(manager+16)==UINT64_C(0x41239374);
}
static void sample(FxObservation *s,const FxObservationIO *io) {
    uint8_t *record=s->data+s->used,*payloads[FX_OBS_SEGMENTS];
    memset(record,0,FX_OBS_RECORD_BYTES);
    memcpy(record,"FXOR",4);fx_ptp_put32(record+4,1u);fx_ptp_put32(record+8,FX_OBS_RECORD_BYTES);
    fx_ptp_put32(record+12,s->records);fx_ptp_put64(record+16,s->id);
    fx_ptp_put64(record+24,s->boot);fx_ptp_put64(record+32,s->activation);
    fx_ptp_put64(record+40,io->clock(io->context));fx_ptp_put64(record+56,s->frequency);
    fx_ptp_put32(record+64,1u); /* periodic software-context trigger, never REC */
    fx_ptp_put32(record+68,1u);fx_ptp_put32(record+72,FX_OBS_SEGMENTS);
    memcpy(record+96,io->build,32u);
    uint32_t offset=128u,errors=0;
    for(uint32_t i=0;i<FX_OBS_SEGMENTS;++i) {
        uint8_t *h=record+offset,*p=h+48u;uint64_t par=1;
        payloads[i]=p;fx_ptp_put64(h,segments[i].address);fx_ptp_put32(h+8,segments[i].bytes);
        fx_ptp_put64(h+16,io->clock(io->context));
        int copied=!atomic_load_explicit(&s->stop,memory_order_acquire) &&
            io->read(io->context,segments[i].address,segments[i].bytes,p,&par);
        fx_ptp_put32(h+12,copied?segments[i].bytes:0u);
        fx_ptp_put64(h+24,io->clock(io->context));fx_ptp_put64(h+32,par);
        fx_ptp_put32(h+40,copied?0u:1u);if(!copied)++errors;
        offset+=48u+segments[i].bytes;
    }
    fx_ptp_put64(record+48,io->clock(io->context));fx_ptp_put32(record+76,errors);
    /* This is equality of two time-separated observations, not atomicity or
     * frame association. Raw before/after segments remain in the record. */
    fx_ptp_put32(record+80,!errors && !memcmp(payloads[0],payloads[8],408u) &&
        !memcmp(payloads[4],payloads[9],192u)?1u:0u);
    s->used+=offset;++s->records;
    if(errors || !source_template(payloads[3],payloads[5])) {
        s->error=FX_PTP_INTEGRITY;fx_observation_cancel(s,FX_OBS_SOURCE_ERROR);
    }
}
void fx_observation_step(FxObservation *s,const FxObservationIO *io,int allowed) {
    if(!lock(s))return;
    if(s->phase!=FX_OBS_ARMED && s->phase!=FX_OBS_RUNNING){unlock(s);return;}
    if(!allowed || s->boot!=io->boot || s->activation!=io->activation)
        fx_observation_cancel(s,FX_OBS_LOCAL_REVOKE);
    if(!(s->flags&FX_OBS_ALLOW_DISCONNECT) && s->connection!=io->connection)
        fx_observation_cancel(s,FX_OBS_DISCONNECTED);
    uint64_t now=io->clock(io->context);uint32_t reason=atomic_load_explicit(&s->stop,memory_order_acquire);
    if(reason) {finish(s,reason==FX_OBS_SOURCE_ERROR?FX_OBS_FAULT:FX_OBS_CANCELLED,reason,now);unlock(s);return;}
    if(now>=s->deadline){finish(s,FX_OBS_EXPIRED,FX_OBS_LIMIT_TIME,now);unlock(s);return;}
    if(now<s->next){unlock(s);return;}
    s->phase=FX_OBS_RUNNING;
    if(FX_OBS_RECORD_BYTES>FX_OBS_BYTES-s->used) {
        finish(s,FX_OBS_COMPLETE,FX_OBS_LIMIT_BYTES,now);unlock(s);return;
    }
    sample(s,io);now=io->clock(io->context);
    reason=atomic_load_explicit(&s->stop,memory_order_acquire);
    if(reason)finish(s,reason==FX_OBS_SOURCE_ERROR?FX_OBS_FAULT:FX_OBS_CANCELLED,reason,now);
    else if(s->records>=s->maximum)finish(s,FX_OBS_COMPLETE,FX_OBS_LIMIT_COUNT,now);
    else if(now>=s->deadline)finish(s,FX_OBS_EXPIRED,FX_OBS_LIMIT_TIME,now);
    else {
        uint64_t delay=s->frequency*s->period_ms/1000u;
        s->next=now>UINT64_MAX-delay?UINT64_MAX:now+delay; /* no catch-up burst */
    }
    unlock(s);
}
int fx_observation_quiesced(FxObservation *s) {
    if(!lock(s))return 0;
    int quiet=s->phase==FX_OBS_IDLE || terminal(s->phase);unlock(s);return quiet;
}
static void status(const FxObservation *s,const FxObservationIO *io,uint8_t *out,uint32_t *n) {
    memset(out,0,FX_OBS_STATUS_BYTES);memcpy(out,"FXO1",4);fx_ptp_put32(out+4,1u);
    fx_ptp_put32(out+8,FX_OBS_STATUS_BYTES);fx_ptp_put32(out+12,s->phase);
    fx_ptp_put64(out+16,s->id);fx_ptp_put64(out+24,s->boot);fx_ptp_put64(out+32,s->activation);
    memcpy(out+40,s->nonce,16u);fx_ptp_put32(out+56,1u);fx_ptp_put32(out+60,s->flags);
    fx_ptp_put32(out+64,s->period_ms);fx_ptp_put32(out+68,s->duration_ms);
    fx_ptp_put32(out+72,s->maximum);fx_ptp_put32(out+76,s->records);
    fx_ptp_put32(out+80,s->used);fx_ptp_put32(out+84,FX_OBS_BYTES);
    fx_ptp_put32(out+88,s->reason);fx_ptp_put32(out+92,s->error);
    fx_ptp_put64(out+96,s->start);fx_ptp_put64(out+104,s->end);
    fx_ptp_put64(out+112,s->frequency);
    if(terminal(s->phase))memcpy(out+128,s->digest,32u);
    memcpy(out+160,io->build,32u);*n=FX_OBS_STATUS_BYTES;
}
static int ticket(const FxObservation *s,const FxObservationIO *io,const uint8_t *in) {
    return s->phase!=FX_OBS_IDLE && fx_ptp_get64(in)==s->id &&
        fx_ptp_get64(in+8)==s->boot && fx_ptp_get64(in+16)==s->activation &&
        s->boot==io->boot && s->activation==io->activation;
}
uint32_t fx_observation_request(FxObservation *s,const FxObservationIO *io,
    const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    *n=0;
    if(bytes<4u || !io->clock || !io->read || !io->frequency ||
       io->frequency>UINT64_C(10000000000) || !io->boot || !io->activation)return FX_PTP_ARGUMENT;
    uint32_t op=fx_ptp_get32(in);in+=4u;bytes-=4u;
    if(op==FX_OBS_TRACE || op==FX_OBS_FRAME)return FX_PTP_UNSUPPORTED;
    if(!lock(s))return FX_PTP_BUSY;
    uint32_t result=FX_PTP_ARGUMENT;
    if(op==FX_OBS_ARM || op==FX_OBS_SNAPSHOT) {
        if(bytes!=40u)goto done;
        uint32_t template=fx_ptp_get32(in+16),period=fx_ptp_get32(in+20),duration=fx_ptp_get32(in+24),
            maximum=fx_ptp_get32(in+28),flags=fx_ptp_get32(in+32);
        uint8_t nonzero=0;for(uint32_t i=0;i<16u;++i)nonzero|=in[i];
        if(!nonzero || template!=1u || period<100u || period>30000u || !duration || duration>30000u ||
           !maximum || maximum>FX_OBS_RECORDS || flags>FX_OBS_ALLOW_DISCONNECT || fx_ptp_get32(in+36) ||
           (op==FX_OBS_SNAPSHOT && (maximum!=1u || flags)))goto done;
        if(s->phase!=FX_OBS_IDLE && !memcmp(s->nonce,in,16u)) {
            if(s->boot!=io->boot || s->activation!=io->activation){result=FX_PTP_STALE;goto done;}
            if(s->period_ms!=period || s->duration_ms!=duration || s->maximum!=maximum || s->flags!=flags)goto done;
            status(s,io,out,n);result=FX_PTP_OK;goto done;
        }
        for(uint32_t i=0;i<s->jobs;++i)if(!memcmp(s->nonces[i],in,16u)){result=FX_PTP_STALE;goto done;}
        if(s->phase!=FX_OBS_IDLE){result=FX_PTP_BUSY;goto done;}
        if(s->jobs==FX_OBS_JOBS){result=FX_PTP_BUSY;goto done;}
        uint64_t now=io->clock(io->context),span=io->frequency*duration/1000u;
        if(!span || now>UINT64_MAX-span)goto done;
        uint64_t id=capture_id(s,io,in);
        if(id==s->id){result=FX_PTP_INTEGRITY;goto done;}
        s->id=id;memcpy(s->nonce,in,16u);memcpy(s->nonces[s->jobs++],in,16u);
        s->boot=io->boot;s->activation=io->activation;s->frequency=io->frequency;s->connection=io->connection;
        s->period_ms=period;s->duration_ms=duration;s->maximum=maximum;s->flags=flags;
        s->records=0;s->used=0;s->error=0;s->reason=0;s->start=now;s->end=0;
        s->next=now;s->deadline=now+span;memset(s->digest,0,sizeof(s->digest));
        atomic_store(&s->stop,0u);s->phase=FX_OBS_ARMED;status(s,io,out,n);result=FX_PTP_OK;
    } else if(op==FX_OBS_FIND) {
        if(bytes!=16u)goto done;
        if(s->phase==FX_OBS_IDLE || memcmp(s->nonce,in,16u) ||
           s->boot!=io->boot || s->activation!=io->activation){result=FX_PTP_STALE;goto done;}
        status(s,io,out,n);result=FX_PTP_OK;
    } else if(op>=FX_OBS_STATUS && op<=FX_OBS_RELEASE) {
        if(bytes!=(op==FX_OBS_READ?32u:24u))goto done;
        if(!ticket(s,io,in)){result=FX_PTP_STALE;goto done;}
        if(op==FX_OBS_STATUS){status(s,io,out,n);result=FX_PTP_OK;}
        else if(op==FX_OBS_STOP){fx_observation_cancel(s,FX_OBS_HOST_STOP);status(s,io,out,n);result=FX_PTP_OK;}
        else if(!terminal(s->phase)){result=FX_PTP_BUSY;}
        else if(op==FX_OBS_RELEASE){s->phase=FX_OBS_IDLE;result=FX_PTP_OK;}
        else {
            uint32_t at=fx_ptp_get32(in+24),count=fx_ptp_get32(in+28);
            if(!count || count>1008u || at>s->used || count>s->used-at)goto done;
            memcpy(out,s->data+at,count);*n=count;result=FX_PTP_OK;
        }
    }
done:unlock(s);return result;
}
