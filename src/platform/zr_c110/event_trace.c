/* Fixed-event recorder. Association requires an observed function entry,
 * original kernel task identity AND an exact native stack-depth match.
 * Any dropped callback invalidates every outstanding association. */
#include "event_trace.h"
#include "fenestra_memory.h"
#include "fenestra_ptp.h"
#include "fenestra_sha256.h"
static int lock(FxEventTrace *s) {
    uint32_t z=0;return atomic_compare_exchange_strong_explicit(&s->lock,&z,1u,memory_order_acquire,memory_order_relaxed);
}
static void unlock(FxEventTrace *s){atomic_store_explicit(&s->lock,0u,memory_order_release);}
void fx_event_trace_init(FxEventTrace *s){memset(s,0,sizeof(*s));}
void fx_event_trace_cancel(FxEventTrace *s,uint32_t reason) {
    if(!s)return;
    atomic_store_explicit(&s->accepting,0u,memory_order_release);
    uint32_t z=0;(void)atomic_compare_exchange_strong_explicit(&s->stop,&z,reason,memory_order_release,memory_order_relaxed);
}
static void gap(FxEventTrace *s) {
    atomic_fetch_add_explicit(&s->dropped,1u,memory_order_relaxed);
    atomic_fetch_add_explicit(&s->association,1u,memory_order_release);
}
int fx_event_trace_begin(FxEventTrace *s,uint32_t template_id) {
    if(!s || template_id>=64u)return 0;
    uint32_t admission=atomic_load_explicit(&s->accepting,memory_order_acquire);
    if(!admission)return 0;
    atomic_fetch_add_explicit(&s->callbacks,1u,memory_order_acquire);
    /* A callback preempted before pinning must not join a different capture
     * after STOP/RELEASE/ARM (accepting=true would permit that ABA race). */
    if(atomic_load_explicit(&s->accepting,memory_order_acquire)!=admission || !(s->mask&(UINT64_C(1)<<template_id))) {
        atomic_fetch_sub_explicit(&s->callbacks,1u,memory_order_release);return 0;
    }
    if((s->flags&FX_ET_WAIT_REC_START) && template_id!=15u &&
       !atomic_load_explicit(&s->rec_started,memory_order_acquire)) {
        atomic_fetch_sub_explicit(&s->callbacks,1u,memory_order_release);return 0;
    }
    return 1;
}
static int authorized(FxEventTrace *s,const FxObservationIO *io,int admitted) {
    uint32_t reason=0;
    if(!admitted || s->boot!=io->boot || s->activation!=io->activation)reason=FX_OBS_LOCAL_REVOKE;
    else if(s->frequency!=io->frequency){reason=FX_OBS_SOURCE_ERROR;s->error=FX_PTP_INTEGRITY;}
    else if(!(s->flags&FX_OBS_ALLOW_DISCONNECT) && s->connection!=io->connection)reason=FX_OBS_DISCONNECTED;
    else if(io->clock(io->context)>=s->deadline)reason=FX_OBS_LIMIT_TIME;
    if(reason)fx_event_trace_cancel(s,reason);
    return !reason && !atomic_load_explicit(&s->stop,memory_order_acquire);
}
void fx_event_trace_append(FxEventTrace *s,const FxObservationIO *io,const FxEventInput *in,int admitted) {
    if(!lock(s)){gap(s);goto leave;}
    if(!authorized(s,io,admitted) || s->records>=s->maximum)goto done;
    if(in->bytes>FX_ET_PAYLOAD || in->template_id>=64u || in->kind<FX_ET_ENTRY || in->kind>FX_ET_TAIL) {
        s->error=FX_PTP_INTEGRITY;fx_event_trace_cancel(s,FX_OBS_SOURCE_ERROR);goto done;
    }
    uint32_t association=atomic_load_explicit(&s->association,memory_order_acquire);
    uint32_t flags=in->flags|FX_ET_SOFTWARE_ONLY;
    uint64_t token=0,parent=0;FxEventScope *current=0,*ancestor=0,*vacant=0;
    if(in->thread && !(flags&FX_ET_CONTEXT_UNKNOWN)) {
        for(uint32_t i=0;i<FX_ET_SCOPES;++i) {
            FxEventScope *p=&s->scope[i];
            if(p->used && p->association!=association){p->used=0;--s->scopes;++s->orphans;}
            if(!p->used){if(!vacant)vacant=p;continue;}
            if(p->thread!=in->thread)continue;
            if(in->kind==FX_ET_ENTRY) {
                if(p->sp<=in->entry_sp && !(p->tail && p->sp==in->entry_sp && in->template_id==12u)) {
                    p->used=0;--s->scopes;++s->orphans;flags|=FX_ET_ORPHAN;
                    if(!vacant)vacant=p;
                    continue;
                }
                if(!ancestor || p->token>ancestor->token)ancestor=p;
            } else if(p->template_id==in->template_id && p->sp==in->entry_sp)current=p;
        }
        if(in->kind==FX_ET_ENTRY && vacant) {
            current=vacant;memset(current,0,sizeof(*current));current->used=1u;++s->scopes;
            current->thread=in->thread;current->sp=in->entry_sp;
            current->token=(uint64_t)s->generation<<32|++s->serial;current->parent=ancestor?ancestor->token:0;
            current->template_id=in->template_id;current->association=association;
            memcpy(current->args,in->registers,4u*sizeof(uint64_t));
        }
        if(current) {
            token=current->token;parent=current->parent;
            if(in->kind==FX_ET_TAIL)current->tail=1u;
            if(in->kind==FX_ET_EXIT) {
                current->used=0;--s->scopes;
                /* A known tail to the batch function finishes at its actual
                 * return; no guessed timestamp/slot or synthetic frame ID. */
                if(in->template_id==12u && parent)for(uint32_t i=0;i<FX_ET_SCOPES;++i) {
                    FxEventScope *p=&s->scope[i];
                    if(p->used && p->tail && p->token==parent && p->thread==in->thread && p->sp==in->entry_sp) {
                        p->used=0;--s->scopes;break;
                    }
                }
            }
        } else flags|=FX_ET_SCOPE_UNKNOWN;
    } else flags|=FX_ET_CONTEXT_UNKNOWN|FX_ET_SCOPE_UNKNOWN;
    if(!parent)flags|=FX_ET_PARENT_UNKNOWN;
    if(association)flags|=FX_ET_GAP;
    if((flags&FX_ET_REC_START) && (!token || in->template_id!=15u || in->event!=10u ||
       flags&(FX_ET_SOURCE_ERROR|FX_ET_SCOPE_UNKNOWN|FX_ET_CONTEXT_UNKNOWN|FX_ET_GAP|FX_ET_ORPHAN)))
        flags&=~(uint32_t)FX_ET_REC_START;
    uint8_t *r=s->data+(size_t)s->records*FX_ET_RECORD;memset(r,0,FX_ET_RECORD);
    memcpy(r,"FXER",4u);fx_ptp_put32(r+4,1u);fx_ptp_put32(r+8,FX_ET_RECORD);fx_ptp_put32(r+12,s->records++);
    fx_ptp_put64(r+16,s->id);fx_ptp_put64(r+24,s->boot);fx_ptp_put64(r+32,s->activation);
    fx_ptp_put64(r+40,in->begin);fx_ptp_put64(r+48,in->end);fx_ptp_put64(r+56,s->frequency);
    fx_ptp_put64(r+64,token);fx_ptp_put64(r+72,parent);fx_ptp_put64(r+80,in->thread);
    fx_ptp_put64(r+88,in->pc);fx_ptp_put64(r+96,in->sp);fx_ptp_put64(r+104,in->kernel);fx_ptp_put64(r+112,in->cpu);
    fx_ptp_put64(r+120,in->entry_sp);fx_ptp_put32(r+128,in->template_id);fx_ptp_put32(r+132,in->event);
    fx_ptp_put32(r+136,flags);fx_ptp_put32(r+140,in->bytes);fx_ptp_put32(r+144,association);fx_ptp_put32(r+148,in->reads);
    fx_ptp_put32(r+152,in->kind);memcpy(r+160,io->build,32u);
    for(uint32_t i=0;i<16u;++i)fx_ptp_put64(r+192u+i*8u,in->registers[i]);
    memcpy(r+320,in->payload,in->bytes);s->phase=FX_OBS_RUNNING;
    if((s->flags&FX_ET_WAIT_REC_START) && (flags&FX_ET_REC_START))
        atomic_store_explicit(&s->rec_started,1u,memory_order_release);
    if(s->records>=s->maximum)fx_event_trace_cancel(s,FX_OBS_LIMIT_COUNT);
done:unlock(s);
leave:atomic_fetch_sub_explicit(&s->callbacks,1u,memory_order_release);
}
void fx_event_trace_step(FxEventTrace *s,const FxObservationIO *io,int admitted) {
    if(!s || !lock(s))return;
    if(s->phase!=FX_OBS_ARMED && s->phase!=FX_OBS_RUNNING && s->phase!=FX_OBS_STOPPING){unlock(s);return;}
    (void)authorized(s,io,admitted);uint32_t reason=atomic_load_explicit(&s->stop,memory_order_acquire);
    if(!reason){unlock(s);return;}s->phase=FX_OBS_STOPPING;s->reason=reason;
    if(atomic_load_explicit(&s->callbacks,memory_order_acquire)){unlock(s);return;}
    fx_sha256(s->data,s->records*FX_ET_RECORD,s->digest);s->end=io->clock(io->context);
    s->phase=reason==FX_OBS_SOURCE_ERROR?FX_OBS_FAULT:reason==FX_OBS_LIMIT_TIME?FX_OBS_EXPIRED:
        (reason==FX_OBS_LIMIT_COUNT || reason==FX_OBS_HOST_STOP)?FX_OBS_COMPLETE:FX_OBS_CANCELLED;
    unlock(s);
}
int fx_event_trace_quiesced(const FxEventTrace *s) {
    return !s || (!atomic_load_explicit(&s->accepting,memory_order_acquire) && !atomic_load_explicit(&s->callbacks,memory_order_acquire));
}
static uint32_t status(FxEventTrace *s,const FxObservationIO *io,uint8_t *out,uint32_t *n) {
    memset(out,0,192u);memcpy(out,"FXEC",4u);fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,192u);fx_ptp_put32(out+12,s->phase);
    fx_ptp_put64(out+16,s->id);fx_ptp_put64(out+24,s->boot);fx_ptp_put64(out+32,s->activation);memcpy(out+40,s->nonce,16u);
    uint32_t flags=s->flags|(atomic_load_explicit(&s->rec_started,memory_order_acquire)?4u:0u);
    fx_ptp_put32(out+56,(uint32_t)s->mask);fx_ptp_put32(out+60,flags);fx_ptp_put32(out+64,s->duration_ms);fx_ptp_put32(out+68,s->maximum);
    fx_ptp_put32(out+72,s->records);fx_ptp_put32(out+76,s->scopes);fx_ptp_put32(out+80,s->records*FX_ET_RECORD);fx_ptp_put32(out+84,FX_ET_BYTES);
    fx_ptp_put32(out+88,s->reason);fx_ptp_put32(out+92,s->error);fx_ptp_put64(out+96,s->start);fx_ptp_put64(out+104,s->end);fx_ptp_put64(out+112,s->frequency);
    fx_ptp_put32(out+120,atomic_load_explicit(&s->dropped,memory_order_relaxed));fx_ptp_put32(out+124,s->orphans);
    memcpy(out+128,s->digest,32u);memcpy(out+160,io->build,32u);*n=192u;return FX_PTP_OK;
}
uint32_t fx_event_trace_request(FxEventTrace *s,const FxObservationIO *io,uint64_t supported,
    const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    *n=0;if(!s || !io || bytes<4u)return FX_PTP_ARGUMENT;
    if(!lock(s))return FX_PTP_BUSY;
    uint32_t op=fx_ptp_get32(in),result=FX_PTP_ARGUMENT;
    if(op==1u && bytes==44u) {
        uint32_t duration=fx_ptp_get32(in+20),maximum=fx_ptp_get32(in+24),flags=fx_ptp_get32(in+28);
        uint64_t mask=fx_ptp_get64(in+36);uint8_t nonzero=0;for(uint32_t i=4u;i<20u;++i)nonzero|=in[i];
        if(!nonzero || !duration || duration>30000u || !maximum || maximum>FX_ET_RECORDS ||
           flags&~(FX_OBS_ALLOW_DISCONNECT|FX_ET_WAIT_REC_START) || fx_ptp_get32(in+32) || !mask || mask&~supported ||
           ((flags&FX_ET_WAIT_REC_START) && !(mask&(UINT64_C(1)<<15))) ||
           !io->frequency || !io->boot || !io->activation)goto done;
        for(uint32_t i=0;i<s->jobs;++i)if(!memcmp(s->nonces[i],in+4,16u)) {
            result=s->phase!=FX_OBS_IDLE && s->boot==io->boot && s->activation==io->activation &&
                !memcmp(s->nonce,in+4,16u)?status(s,io,out,n):FX_PTP_STALE;goto done;
        }
        if(s->phase!=FX_OBS_IDLE || s->jobs>=FX_OBS_JOBS){result=FX_PTP_BUSY;goto done;}
        if(atomic_load_explicit(&s->callbacks,memory_order_acquire)){result=FX_PTP_BUSY;goto done;}
        uint64_t now=io->clock(io->context);
        if(io->frequency>UINT64_MAX/30000u || !(io->frequency*duration/1000u) || now>UINT64_MAX-io->frequency*30000u/1000u)goto done;
        memcpy(s->nonce,in+4,16u);memcpy(s->nonces[s->jobs++],in+4,16u);++s->generation;
        s->duration_ms=duration;s->maximum=maximum;s->flags=flags;s->mask=mask;
        s->boot=io->boot;s->activation=io->activation;s->connection=io->connection;s->frequency=io->frequency;
        s->start=now;s->end=0;s->deadline=now+io->frequency*duration/1000u;
        uint8_t identity[80],digest[32];memcpy(identity,s->nonce,16u);memcpy(identity+16,io->build,32u);
        fx_ptp_put64(identity+48,s->boot);fx_ptp_put64(identity+56,s->activation);fx_ptp_put64(identity+64,s->generation);memcpy(identity+72,"FXEVENT1",8u);
        fx_sha256(identity,sizeof(identity),digest);s->id=fx_ptp_get64(digest);if(!s->id)s->id=1;
        s->records=s->reason=s->error=s->scopes=s->serial=s->orphans=0;memset(s->scope,0,sizeof(s->scope));memset(s->digest,0,32u);
        atomic_store(&s->stop,0u);atomic_store(&s->dropped,0u);atomic_store(&s->association,0u);atomic_store(&s->rec_started,0u);
        s->phase=FX_OBS_ARMED;atomic_store_explicit(&s->accepting,s->generation,memory_order_release);result=status(s,io,out,n);
    } else if(op==6u && bytes==20u) {
        result=s->phase!=FX_OBS_IDLE && s->boot==io->boot && s->activation==io->activation &&
            !memcmp(s->nonce,in+4,16u)?status(s,io,out,n):FX_PTP_STALE;
    } else if(bytes>=28u) {
        if(s->phase==FX_OBS_IDLE || fx_ptp_get64(in+4)!=s->id || fx_ptp_get64(in+12)!=s->boot ||
           fx_ptp_get64(in+20)!=s->activation || s->boot!=io->boot || s->activation!=io->activation){result=FX_PTP_STALE;goto done;}
        if(op==2u && bytes==28u)result=status(s,io,out,n);
        else if(op==4u && bytes==28u){fx_event_trace_cancel(s,FX_OBS_HOST_STOP);result=status(s,io,out,n);}
        else if(op==5u && bytes==28u) {
            if(s->phase<FX_OBS_COMPLETE || !fx_event_trace_quiesced(s))result=FX_PTP_BUSY;
            else {s->phase=FX_OBS_IDLE;result=FX_PTP_OK;}
        } else if(op==3u && bytes==36u) {
            uint32_t offset=fx_ptp_get32(in+28),count=fx_ptp_get32(in+32),used=s->records*FX_ET_RECORD;
            if(s->phase<FX_OBS_COMPLETE)result=FX_PTP_BUSY;
            else if(count && count<=1008u && offset<=used && count<=used-offset){memcpy(out,s->data+offset,count);*n=count;result=FX_PTP_OK;}
        }
    }
done:unlock(s);return result;
}
