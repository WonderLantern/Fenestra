/* Direct developer sampling. No pool/IRQ/bank/generation/lease qualification.
 * Exported bytes are explicitly unqualified, never a retained frame receipt. */
#include "frame_sample_native.h"
#include "event_trace_native.h"
#include "fenestra_c110_observation.h"
#include "fenestra_c110_rtos.h"
#include "fenestra_memory.h"
#include "fenestra_ptp.h"
#include "fenestra_sha256.h"
#include "c110_debug_identity.h"
#include <stddef.h>
#define UNCHECKED 32u
static FxFrameSample *state(void){FxEventTrace *s=fx_c110_menu_event_trace();return s?&s->frame:0;}
static int take(FxFrameSample *s){uint32_t z=0;return atomic_compare_exchange_strong(&s->lock,&z,1u);}
static void give(FxFrameSample *s){atomic_store_explicit(&s->lock,0u,memory_order_release);}
static uint64_t clock_now(void){uint64_t t;__asm__ volatile("mrs %0,cntpct_el0":"=r"(t));return t;}
static uint64_t frequency(void){uint64_t t;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(t));return t;}
static int read(uint64_t a,uint32_t n,uint8_t *out){uint64_t par;return fx_c110_rtos_read_native(a,n,out,&par);}
static void observe(FxFrameSample *s,FxSensorTraceFrame *f,uint64_t pc) {
    if(pc==UINT64_C(0x40f73370)){s->rec_ticks=clock_now();atomic_store(&s->rec_started,1u);return;}
    if(pc!=UINT64_C(0x4108b028))return;
    if((s->flags&FX_FS_WAIT_REC) && !atomic_load(&s->rec_started))return;
    uint32_t empty=0;
    if(!atomic_compare_exchange_strong(&s->input_state,&empty,1u))return;
    uint8_t pointer[8],descriptor[72],token[16],outer[40],input[32];
    FxFrameInput item={0};item.job=f->x[20];
    (void)read(item.job+0x100u,8u,pointer);item.descriptor=fx_ptp_get64(pointer);
    (void)read(item.descriptor,72u,descriptor);
    (void)read(fx_ptp_get64(descriptor+8u),16u,token);
    item.base=fx_ptp_get64(token);item.capacity=fx_ptp_get64(token+8u);
    item.native_type=fx_ptp_get32(descriptor+48u);item.format=fx_ptp_get32(descriptor+52u);
    item.width=(uint32_t)descriptor[56]|(uint32_t)descriptor[57]<<8;
    item.height=(uint32_t)descriptor[58]|(uint32_t)descriptor[59]<<8;
    (void)read(f->x[0],40u,outer);uint64_t raw=fx_ptp_get64(outer+16u);
    uint64_t image=fx_ptp_get64(outer+8u);
    item.source_type=raw?FX_FS_PACKET:FX_FS_NATIVE_ROWS;
    if(item.source_type!=s->source){atomic_store(&s->input_state,0u);return;}
    (void)read(raw?raw:image,32u,input);
    if(raw)item.data=fx_ptp_get64(input+16u);
    else {
        item.data=fx_ptp_get64(input+24u);item.input_word0=fx_ptp_get32(input);item.input_word1=fx_ptp_get32(input+4u);
        item.stride=(uint32_t)input[8]|(uint32_t)input[9]<<8;
        item.width=(uint32_t)input[10]|(uint32_t)input[11]<<8;
        item.height=(uint32_t)input[12]|(uint32_t)input[13]<<8;
    }
    item.consumed=clock_now();s->input_pending=item;
    atomic_store_explicit(&s->input_state,2u,memory_order_release);
}
void fx_c110_frame_observe(FxSensorTraceFrame *f,uint64_t pc) {
    FxFrameSample *s=state();if(!s || !atomic_load_explicit(&s->accepting,memory_order_acquire) || !take(s))return;
    observe(s,f,pc);give(s);
}
static void finish(FxFrameSample *s,uint32_t reason) {
    s->reason=reason;s->end=clock_now();atomic_store(&s->accepting,0u);
    fx_sha256(s->data,s->used,s->digest);atomic_store(&s->phase,FX_OBS_COMPLETE);
}
static void step(FxFrameSample *s,const FxObservationIO *io) {
    if(atomic_load(&s->stop)){finish(s,atomic_load(&s->stop));return;}
    if(s->duration_ms && io->clock(io->context)>=s->deadline){finish(s,FX_OBS_LIMIT_TIME);return;}
    uint32_t ready=2u;if(!atomic_compare_exchange_strong(&s->input_state,&ready,3u))return;
    FxFrameInput c=s->input_pending;s->candidate=c;
    uint32_t rows=s->source==FX_FS_PACKET?1u:s->height;
    uint32_t row_bytes=s->source==FX_FS_PACKET?s->count:s->width*7u/4u;
    uint64_t first=c.data+(s->source==FX_FS_PACKET?s->offset:(uint64_t)s->y*c.stride+(uint64_t)s->x*7u/4u);
    uint32_t used=FX_FS_HEADER;
    for(uint32_t row=0;row<rows;++row)for(uint32_t at=0;at<row_bytes;) {
        uint32_t n=row_bytes-at;if(n>1008u)n=1008u;
        (void)read(first+(uint64_t)row*c.stride+at,n,s->data+used);used+=n;at+=n;
    }
    uint8_t *p=s->data;memset(p,0,FX_FS_HEADER);memcpy(p,"FXFB",4u);
    fx_ptp_put32(p+4,1u);fx_ptp_put32(p+8,FX_FS_HEADER);fx_ptp_put32(p+12,used);
    fx_ptp_put64(p+16,s->id);fx_ptp_put64(p+24,s->boot);fx_ptp_put64(p+32,s->activation);
    fx_ptp_put64(p+40,s->generation);fx_ptp_put64(p+48,c.base);fx_ptp_put64(p+56,c.capacity);
    fx_ptp_put64(p+64,c.data);fx_ptp_put64(p+72,c.job);fx_ptp_put64(p+80,c.descriptor);fx_ptp_put64(p+88,c.consumed);
    const uint32_t fields[16]={s->source,c.native_type,c.format,c.width,c.height,c.stride,s->x,s->y,
        s->width,s->height,row_bytes,rows,s->offset,c.source_format,0u,64u|(atomic_load(&s->rec_started)?16u:0u)};
    for(uint32_t i=0;i<16u;++i)fx_ptp_put32(p+96u+i*4u,fields[i]);
    memcpy(p+160,io->build,32u);fx_ptp_put64(p+192,clock_now());fx_ptp_put64(p+200,s->rec_ticks);
    fx_ptp_put32(p+208,c.input_word0);fx_ptp_put32(p+212,c.input_word1);
    s->used=used;finish(s,FX_OBS_LIMIT_COUNT);atomic_store(&s->input_state,0u);
}
void fx_c110_frame_step(const FxObservationIO *io,int admitted) {
    (void)admitted;FxFrameSample *s=state();if(!s || !atomic_load(&s->accepting) || !take(s))return;
    step(s,io);give(s);
}
void fx_c110_frame_cancel(uint32_t reason){FxFrameSample *s=state();if(s)atomic_store(&s->stop,reason);}
int fx_c110_frame_quiesced(void){FxFrameSample *s=state();return !s || (!atomic_load(&s->accepting) && !atomic_load(&s->lock));}
static uint32_t status(FxFrameSample *s,uint8_t *out,uint32_t *n) {
    memset(out,0,224u);memcpy(out,"FXFC",4u);fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,224u);
    fx_ptp_put32(out+12,atomic_load(&s->phase));fx_ptp_put64(out+16,s->id);fx_ptp_put64(out+24,s->boot);
    fx_ptp_put64(out+32,s->activation);memcpy(out+40,s->nonce,16u);
    fx_ptp_put32(out+56,s->source);fx_ptp_put32(out+60,s->flags|UNCHECKED|(atomic_load(&s->rec_started)?8u:0u));
    fx_ptp_put32(out+64,s->duration_ms);fx_ptp_put32(out+68,s->used);fx_ptp_put32(out+72,s->reason);
    fx_ptp_put64(out+80,s->start);fx_ptp_put64(out+88,s->end);fx_ptp_put64(out+96,s->frequency);
    fx_ptp_put32(out+116,(uint32_t)!!s->candidate.data);memcpy(out+128,s->digest,32u);
    memcpy(out+160,fx_c110_debug_identity,32u);
    const uint32_t roi[6]={s->x,s->y,s->width,s->height,s->offset,s->count};
    for(uint32_t i=0;i<6u;++i)fx_ptp_put32(out+192u+i*4u,roi[i]);
    *n=224u;return FX_PTP_OK;
}
static uint32_t request(FxFrameSample *s,const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    uint32_t op=fx_ptp_get32(in);
    if(!op) {
        memset(out,0,192u);memcpy(out,"FXFD",4u);fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,192u);
        fx_ptp_put32(out+12,71u);fx_ptp_put32(out+16,FX_FS_BYTES);fx_ptp_put32(out+20,FX_FS_HEADER);
        fx_ptp_put32(out+24,UINT32_MAX);fx_ptp_put32(out+28,64u);fx_ptp_put32(out+32,8192u);
        memcpy(out+64,fx_c110_debug_identity,32u);*n=192u;return FX_PTP_OK;
    }
    if(op==1u) {
        if(bytes!=64u)return FX_PTP_ARGUMENT;
        uint32_t source=fx_ptp_get32(in+28u),x=fx_ptp_get32(in+32u),y=fx_ptp_get32(in+36u);
        uint32_t w=fx_ptp_get32(in+40u),h=fx_ptp_get32(in+44u),count=fx_ptp_get32(in+52u);
        /* Only fixed-buffer arithmetic remains; no source/format/lease policy. */
        if((source!=1u && source!=2u) || (source==1u?count>8192u:
           (!w || !h || w>64u || h>64u || (uint64_t)(w*7u/4u)*h>FX_FS_BYTES-FX_FS_HEADER)))return FX_PTP_ARGUMENT;
        uint64_t next=s->id+1u;uint32_t gen=s->generation+1u;
        memset((uint8_t *)s+sizeof(s->lock),0,sizeof(*s)-sizeof(s->lock));
        s->id=next?next:1u;s->generation=gen?gen:1u;memcpy(s->nonce,in+4u,16u);
        FxC110Observation *ob=fx_c110_menu_observation();s->boot=ob?ob->boot:1u;s->activation=fx_c110_observation_activation();
        s->duration_ms=fx_ptp_get32(in+20u);s->flags=(fx_ptp_get32(in+24u)&5u)|2u|UNCHECKED;
        s->source=source;s->x=x;s->y=y;s->width=w;s->height=h;s->offset=fx_ptp_get32(in+48u);s->count=count;
        s->frequency=frequency();s->start=clock_now();s->deadline=s->start+s->frequency*s->duration_ms/1000u;
        atomic_store(&s->phase,FX_OBS_ARMED);atomic_store_explicit(&s->accepting,1u,memory_order_release);
        return status(s,out,n);
    }
    if(op==2u || op==6u)return status(s,out,n);
    if(op==4u){fx_c110_frame_cancel(FX_OBS_HOST_STOP);return status(s,out,n);}
    if(op==5u){atomic_store(&s->accepting,0u);atomic_store(&s->phase,FX_OBS_IDLE);return FX_PTP_OK;}
    if(op==3u && bytes==36u) {
        uint32_t at=fx_ptp_get32(in+28u),count=fx_ptp_get32(in+32u);
        if(count>1008u || at>FX_FS_BYTES || count>FX_FS_BYTES-at)return FX_PTP_ARGUMENT;
        memcpy(out,s->data+at,count);*n=count;return FX_PTP_OK;
    }
    return FX_PTP_UNSUPPORTED;
}
uint32_t fx_c110_frame_request(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    *n=0;if(bytes<4u)return FX_PTP_ARGUMENT;
    if(!fx_ptp_get32(in))return request(0,in,bytes,out,n);
    FxEventTrace *owner=fx_c110_menu_event_trace();if(!owner)owner=fx_c110_menu_event_trace_create();
    if(!owner)return FX_PTP_UNSUPPORTED;
    FxFrameSample *s=&owner->frame;if(!take(s))return FX_PTP_BUSY;
    uint32_t result=request(s,in,bytes,out,n);give(s);return result;
}
const uint32_t fx_c110_frame_sample_metadata[]={0x46584650u,2u,sizeof(FxFrameSample),offsetof(FxEventTrace,frame),
    offsetof(FxFrameSample,phase),offsetof(FxFrameSample,data),offsetof(FxFrameSample,used),
    offsetof(FxFrameSample,accepting),offsetof(FxFrameSample,callbacks),offsetof(FxFrameSample,lost),
    offsetof(FxFrameSample,writers),offsetof(FxFrameSample,write_epoch),offsetof(FxFrameSample,native_bank_claim),
    offsetof(FxFrameSample,rec_started),FX_FS_BYTES,FX_FS_HEADER};
