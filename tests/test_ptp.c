#include "fenestra_ptp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static FxPtp p;
static unsigned calls,mode;
static uint8_t request[FX_PTP_FRAME],reply[FX_PTP_FRAME],hello[64];
static uint32_t make(uint64_t session,uint32_t id,uint16_t op,const void *data,uint32_t bytes) {
    memset(request,0,sizeof(request));memcpy(request,"FXP2",4);
    request[4]=2;request[6]=(uint8_t)op;request[7]=(uint8_t)(op>>8);
    fx_ptp_put64(request+8,session);fx_ptp_put32(request+16,id);fx_ptp_put32(request+20,bytes);
    if(bytes) memcpy(request+32,data,bytes);
    fx_ptp_put32(request+28,fx_ptp_crc(request,bytes+32));return bytes+32;
}
static uint32_t backend(void *ctx,uint16_t op,const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    (void)ctx;(void)op;++calls;
    assert(fx_ptp_hello(&p,hello)==FX_PTP_BUSY);
    assert(fx_ptp_submit(&p,request,p.request_bytes)==FX_PTP_OK); /* in-flight retry */
    if(mode==1) {
        fx_ptp_disconnect(&p);
        assert(fx_ptp_hello(&p,hello)==FX_PTP_BUSY);
    }
    if(mode==4) {
        uint8_t control[32];uint64_t session=p.session;uint32_t id=p.request_id;
        assert(fx_ptp_control(&p,session,id,FX_PTP_JOB_CANCEL,control)==FX_PTP_OK);
        assert(p.job==FX_PTP_RUNNING && p.cancel_requested && p.request_bytes);
        assert(fx_ptp_get32(control+12)==FX_PTP_RUNNING && fx_ptp_get32(control+16)==1u);
        assert(fx_ptp_control(&p,session,id,FX_PTP_JOB_RELEASE,control)==FX_PTP_BUSY);
        assert(fx_ptp_hello(&p,hello)==FX_PTP_BUSY);
    }
    if(mode==2) atomic_store(&p.locked,1); /* publication contends with USB */
    memcpy(out,in,bytes);*n=mode==3?FX_PTP_PAYLOAD+1:bytes;
    return FX_PTP_OK;
}
int main(void) {
    fx_ptp_init(&p);
    assert(fx_ptp_hello(&p,hello)==FX_PTP_CLOSED);
    fx_ptp_enable(&p,1);assert(fx_ptp_hello(&p,hello)==0);
    uint64_t session=fx_ptp_get64(hello+8);
    assert(session && fx_ptp_get32(hello+16)==1024 && fx_ptp_get32(hello+32)==0);
    const uint8_t data[]={0,255,0,128,42};
    uint32_t n=make(session,1,2,data,sizeof(data));
    for(uint32_t i=0;i<32;++i) assert(fx_ptp_submit(&p,request,i)==FX_PTP_ARGUMENT);
    assert(fx_ptp_submit(&p,request,FX_PTP_FRAME+1)==FX_PTP_ARGUMENT);
    request[32]^=1;assert(fx_ptp_submit(&p,request,n)==FX_PTP_INTEGRITY);request[32]^=1;
    assert(fx_ptp_submit(&p,request,n)==0);assert(fx_ptp_submit(&p,request,n)==0);
    uint32_t got=99;
    assert(fx_ptp_fetch(&p,session,1,0,reply,sizeof(reply),&got)==FX_PTP_BUSY && !got);
    assert(!calls && fx_ptp_poll(&p,backend,0)==0 && calls==1);
    assert(fx_ptp_submit(&p,request,n)==0 && fx_ptp_poll(&p,backend,0)==0 && calls==1);
    assert(fx_ptp_fetch(&p,session,1,0,reply,3,&got)==0 && got==3);
    assert(fx_ptp_fetch(&p,session,1,3,reply+3,FX_PTP_FRAME-3,&got)==0 && got==n-3);
    assert(!memcmp(reply+32,data,sizeof(data)));
    assert(fx_ptp_crc(reply,n)==fx_ptp_get32(reply+28));
    assert(fx_ptp_get32(reply+24)==0x80000000u);
    assert(fx_ptp_fetch(&p,session,1,n,reply,sizeof(reply),&got)==0 && !got);
    assert(fx_ptp_fetch(&p,session,1,n+1,reply,sizeof(reply),&got)==FX_PTP_ARGUMENT);
    assert(fx_ptp_fetch(&p,session,1,0,reply,0,&got)==FX_PTP_ARGUMENT);
    n=make(session,1,1,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==FX_PTP_STALE);
    n=make(session,2,2,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==0);
    mode=1;assert(fx_ptp_poll(&p,backend,0)==FX_PTP_CLOSED && calls==2 && p.discarded==1);
    assert(fx_ptp_hello(&p,hello)==0);uint64_t second=fx_ptp_get64(hello+8);assert(second!=session);
    assert(fx_ptp_submit(&p,request,n)==FX_PTP_STALE);
    n=make(second,1,2,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==0);
    mode=2;assert(fx_ptp_poll(&p,backend,0)==FX_PTP_BUSY && calls==3);
    assert(atomic_load(&p.completed)==1 && p.job==FX_PTP_RUNNING);
    atomic_store(&p.locked,0);
    assert(fx_ptp_poll(&p,backend,0)==0 && calls==3 && p.job==FX_PTP_DONE);
    n=make(second,2,2,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==0);
    mode=3;assert(fx_ptp_poll(&p,backend,0)==0 && calls==4);
    assert(fx_ptp_fetch(&p,second,2,0,reply,sizeof(reply),&got)==0 && got==32);
    assert(fx_ptp_get32(reply+24)==(0x80000000u|FX_PTP_INTEGRITY));
    fx_ptp_enable(&p,0);assert(fx_ptp_fetch(&p,second,2,0,reply,sizeof(reply),&got)==FX_PTP_CLOSED);
    fx_ptp_enable(&p,1);assert(fx_ptp_hello(&p,hello)==0);
    session=fx_ptp_get64(hello+8);uint8_t control[32];unsigned prior=calls;
    n=make(session,1,2,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==0);
    assert(fx_ptp_control(&p,session+1,1,FX_PTP_JOB_CANCEL,control)==FX_PTP_STALE);
    assert(fx_ptp_control(&p,session,2,FX_PTP_JOB_CANCEL,control)==FX_PTP_STALE);
    assert(fx_ptp_control(&p,session,1,99u,control)==FX_PTP_ARGUMENT);
    assert(fx_ptp_control(&p,session,1,FX_PTP_JOB_RELEASE,control)==FX_PTP_BUSY);
    assert(fx_ptp_control(&p,session,1,FX_PTP_JOB_CANCEL,control)==0);
    assert(p.job==FX_PTP_CANCELLED && !p.request_bytes && !p.response_bytes && !p.completion_bytes);
    assert(fx_ptp_control(&p,session,1,FX_PTP_JOB_CANCEL,control)==0); /* Lost control ACK. */
    assert(fx_ptp_submit(&p,request,n)==FX_PTP_STALE);
    assert(fx_ptp_fetch(&p,session,1,0,reply,sizeof(reply),&got)==FX_PTP_STALE);
    assert(fx_ptp_poll(&p,backend,0)==0 && calls==prior);
    assert(fx_ptp_control(&p,session,1,FX_PTP_JOB_RELEASE,control)==0);
    n=make(session,2,2,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==0);
    mode=4;assert(fx_ptp_poll(&p,backend,0)==0 && calls==prior+1u && p.job==FX_PTP_CANCELLED);
    for(uint32_t i=0;i<FX_PTP_FRAME;++i)assert(!p.request[i] && !p.response[i] && !p.completion[i]);
    assert(fx_ptp_submit(&p,request,n)==FX_PTP_STALE);
    n=make(session,3,2,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==0);
    mode=2;assert(fx_ptp_poll(&p,backend,0)==FX_PTP_BUSY && atomic_load(&p.completed));
    atomic_store(&p.locked,0);
    assert(fx_ptp_control(&p,session,3,FX_PTP_JOB_CANCEL,control)==0);
    assert(fx_ptp_poll(&p,backend,0)==0 && p.job==FX_PTP_CANCELLED && calls==prior+2u);
    n=make(session,4,2,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==0);
    mode=0;assert(fx_ptp_poll(&p,backend,0)==0 && calls==prior+3u);
    assert(fx_ptp_control(&p,session,4,FX_PTP_JOB_STATUS,control)==0 && fx_ptp_get32(control+20)==n);
    assert(fx_ptp_control(&p,session,4,FX_PTP_JOB_RELEASE,control)==0 && p.job==FX_PTP_RELEASED);
    assert(fx_ptp_control(&p,session,4,FX_PTP_JOB_RELEASE,control)==0);
    assert(fx_ptp_submit(&p,request,n)==FX_PTP_STALE);
    for(uint32_t i=0;i<FX_PTP_FRAME;++i)assert(!p.request[i] && !p.response[i] && !p.completion[i]);
    n=make(session,5,2,data,sizeof(data));assert(fx_ptp_submit(&p,request,n)==0);
    fx_ptp_disconnect(&p);
    assert(p.job==FX_PTP_IDLE && !p.request_bytes && !p.response_bytes && !p.completion_bytes);
    assert(fx_ptp_poll(&p,backend,0)==0 && calls==prior+3u);
    assert(fx_ptp_control(&p,session,5,FX_PTP_JOB_STATUS,control)==FX_PTP_STALE);
    p.session_sequence=UINT32_MAX;assert(fx_ptp_hello(&p,hello)==FX_PTP_CLOSED);
    atomic_store(&p.generation,UINT32_MAX-1u);fx_ptp_disconnect(&p);fx_ptp_enable(&p,1);
    assert(atomic_load(&p.generation)==UINT32_MAX && !atomic_load(&p.enabled));
    assert(fx_ptp_hello(&p,hello)==FX_PTP_CLOSED);
    puts("PASS: PTP gating, binary transfer, integrity, replay, publication contention, disconnect, queued/running cancel, result release, exhaustion");
    return 0;
}
