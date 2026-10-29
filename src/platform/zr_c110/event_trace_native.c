/* Trusted fixed-site adapter. No host-defined addresses, calls or callbacks.
 * Memory segments remain non-atomic observations; only saved GP values are
 * the actual native locals at the specified instruction. */
#include "event_trace_native.h"
#include "frame_sample_native.h"
#include "fenestra_c110_observation.h"
#include "fenestra_c110_rtos.h"
#include "fenestra_ptp.h"
#include "fenestra_memory.h"
#include "c110_debug_identity.h"
#include "c110_event_config.h"
static const uint8_t registers[16]={0,1,2,3,8,9,10,11,19,20,21,22,23,24,25,26};
static uint64_t clock_read(void *p){(void)p;uint64_t n;__asm__ volatile("mrs %0,cntpct_el0":"=r"(n));return n;}
static int read_source(void *p,uint64_t a,uint32_t n,uint8_t *d,uint64_t *par){(void)p;return fx_c110_rtos_read_normal(a,n,d,par);}
static int read_native_source(void *p,uint64_t a,uint32_t n,uint8_t *d,uint64_t *par){(void)p;return fx_c110_rtos_read_native(a,n,d,par);}
static int admitted(void){FxPtp *p=fx_c110_menu_ptp();return p && fx_c110_menu_ptp_active() && atomic_load_explicit(&p->enabled,memory_order_acquire);}
static FxObservationIO current(void) {
    FxC110Observation *s=fx_c110_menu_observation();uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
    FxObservationIO io={0,clock_read,read_source,hz,s?s->boot:0,fx_c110_observation_activation(),
        s?atomic_load_explicit(&s->disconnect_epoch,memory_order_acquire):0,{0}};
    memcpy(io.build,fx_c110_debug_identity,32u);return io;
}
static int copy(const FxObservationIO *io,FxEventInput *e,uint64_t a,uint32_t n,uint8_t *out) {
    uint64_t par=1u;++e->reads;return io->read(io->context,a,n,out,&par);
}
static const uint8_t *segment(const FxObservationIO *io,FxEventInput *e,uint16_t label,uint64_t address,uint16_t bytes) {
    if(e->bytes>FX_ET_PAYLOAD-16u || bytes>FX_ET_PAYLOAD-16u-e->bytes){e->flags|=FX_ET_SOURCE_ERROR;return 0;}
    uint8_t *p=e->payload+e->bytes;e->bytes+=16u+bytes;memset(p,0,16u+bytes);
    fx_ptp_put64(p,address);p[8]=(uint8_t)bytes;p[9]=(uint8_t)(bytes>>8);p[12]=(uint8_t)label;p[13]=(uint8_t)(label>>8);
    if(!copy(io,e,address,bytes,p+16u)){p[14]=1u;e->flags|=FX_ET_SOURCE_ERROR;return 0;}
    p[10]=(uint8_t)bytes;p[11]=(uint8_t)(bytes>>8);return p+16u;
}
static void inline_pair(FxEventInput *e,uint16_t label,uint64_t a,uint64_t b) {
    if(e->bytes>FX_ET_PAYLOAD-32u){e->flags|=FX_ET_SOURCE_ERROR;return;}
    uint8_t *p=e->payload+e->bytes;e->bytes+=32u;memset(p,0,32u);
    p[8]=p[10]=16u;p[12]=(uint8_t)label;p[13]=(uint8_t)(label>>8);
    fx_ptp_put64(p+16,a);fx_ptp_put64(p+24,b);
}
static void kernel_identity(const FxObservationIO *io,FxEventInput *e) {
    /* Original kernel4018569c/401865f0/40189174 use TPIDR_EL1->[0]
     * as the current task object; +50 excludes nested/interrupt contexts.
     * The object address is not a reusable frame ID or an eternal task ID. */
    uint8_t task[8],nest[4],again[8];uint64_t now;
    __asm__ volatile("mrs %0,tpidr_el1":"=r"(now));if(now!=e->kernel)goto unknown;
    if(!e->kernel || !copy(io,e,e->kernel,8u,task) || !copy(io,e,e->kernel+0x50u,4u,nest) ||
       fx_ptp_get32(nest) || !copy(io,e,e->kernel,8u,again) || memcmp(task,again,8u))goto unknown;
    uint64_t p=fx_ptp_get64(task);
    if((p&7u) || p<UINT64_C(0xa00000000) || p>UINT64_C(0xa40000000)-0x80u)goto unknown;
    __asm__ volatile("mrs %0,tpidr_el1":"=r"(now));if(now!=e->kernel)goto unknown;
    e->thread=p;return;
unknown:e->flags|=FX_ET_CONTEXT_UNKNOWN;e->thread=0;
}
static void sensor_context(const FxObservationIO *io,FxEventInput *e,uint64_t c) {
    (void)segment(io,e,1u,c,64u);(void)segment(io,e,2u,c+0x148u,8u);
    (void)segment(io,e,3u,c+0x58u,4u);(void)segment(io,e,4u,c+0xd8u,4u);
}
static void payload(const FxObservationIO *io,FxEventInput *e,const FxSensorTraceFrame *f) {
    uint64_t sp=e->sp;uint32_t t=e->template_id,v=e->event;
    if(t==1u) {
        if(v==2u && (f->x[0]&1u)) (void)segment(io,e,1u,sp+0x110u,8u);
        else if(v==3u)(void)segment(io,e,1u,sp+0x110u,8u);
        if(v==6u || v==7u) {
            const uint8_t *p=segment(io,e,6u,f->x[23]+0x40u,8u);
            if(p)(void)segment(io,e,7u,fx_ptp_get64(p)+0xa4u,12u);
        }
        if(v==4u) {
            (void)segment(io,e,2u,sp+0xecu,4u);(void)segment(io,e,3u,f->x[23]+0xe4u,12u);
            (void)segment(io,e,4u,f->x[23]+0x8eu,4u);(void)segment(io,e,5u,f->x[23]+0x30u,24u);
        }
    } else if(t==2u) {
        /* Descriptor RAM only. The saved x9 MMIO base is NEVER dereferenced. */
        const uint8_t *object=segment(io,e,1u,f->x[0],24u);(void)segment(io,e,2u,f->x[1],16u);
        if(object && (fx_ptp_get32(object)!=0x191u || fx_ptp_get64(object+8)!=UINT64_C(0x15081000)))e->flags|=FX_ET_SOURCE_ERROR;
    } else if(t==3u) {
        if(v==2u) {
            (void)segment(io,e,1u,UINT64_C(0xa03a737d8),40u);
            (void)segment(io,e,2u,UINT64_C(0xa03b7c098),16u);
            (void)segment(io,e,3u,sp+0x10u,24u);
        } else if(v==4u)(void)segment(io,e,4u,f->x[8],40u);
        else if(v==5u)(void)segment(io,e,5u,sp+0x28u,1u);
    } else if(t==4u) {
        if(v==1u || v==2u) {
            uint64_t descriptor=v==1u?f->x[0]:f->x[20];const uint8_t *p=segment(io,e,1u,descriptor,16u);
            if(p)(void)segment(io,e,2u,fx_ptp_get64(p),40u);
        } else if(v==6u)(void)segment(io,e,2u,f->x[1],40u);
        else if(v==4u)(void)segment(io,e,3u,sp+0xcu,4u);
    } else if(t==5u) {
        if(v==1u)(void)segment(io,e,1u,f->x[0],40u);
        else if(v==2u)(void)segment(io,e,2u,f->x[1],40u);
        /* Async receipt has its own invocation. Slot reuse never supplies
         * a proven sender/parent token; raw slot/header/destination survive. */
    } else if(t==6u) {
        if(v==1u || v==2u)(void)segment(io,e,1u,sp+0x58u,24u);
        if(v==3u){(void)segment(io,e,2u,f->x[8]+0x1c28u,8u);(void)segment(io,e,3u,sp+0x60u,8u);}
        if(v!=4u)(void)segment(io,e,4u,f->x[19],24u);
    } else if(t==7u) {
        if(v==1u){(void)segment(io,e,1u,f->x[1],16u);(void)segment(io,e,2u,f->x[0]+0xba2u,1u);}
        if(v==3u){(void)segment(io,e,3u,f->x[9]+0x1c28u,8u);(void)segment(io,e,4u,f->x[20],16u);}
    } else if(t==8u) {
        if(v==1u || v==2u) {
            (void)segment(io,e,1u,UINT64_C(0xa03e31718),16u);(void)segment(io,e,2u,UINT64_C(0xa03e3196c),16u);
            (void)segment(io,e,3u,UINT64_C(0xa03e31ab8),12u);(void)segment(io,e,4u,UINT64_C(0xa03e317e0),4u);
        } else if(v==3u)(void)segment(io,e,1u,UINT64_C(0xa03e3171c),8u);
    } else if(t==9u) {
        (void)segment(io,e,1u,UINT64_C(0xa03e31abc),4u);(void)segment(io,e,2u,UINT64_C(0xa03e3171c),8u);
        (void)segment(io,e,3u,UINT64_C(0xa03e31970),8u);
        if(v==2u){(void)segment(io,e,4u,f->x[25],1u);(void)segment(io,e,5u,f->x[27],1u);}
    } else if(t==10u) {
        if(v==1u)(void)segment(io,e,5u,f->x[0],40u);
        else if(v==2u || v==3u)sensor_context(io,e,UINT64_C(0xa02b6f4c8));
    } else if(t==11u) {
        if(v==1u)sensor_context(io,e,f->x[1]);
        if(v==3u){(void)segment(io,e,6u,f->x[0],12u);(void)segment(io,e,7u,f->x[1],6u);}
    } else if(t==12u) {
        if(v==1u)(void)segment(io,e,1u,f->x[1],6u);
        else if(v==2u) {
            uint16_t n=(uint16_t)(f->x[1]>64u?64u:f->x[1]);if(n)(void)segment(io,e,2u,f->x[0],n);
            inline_pair(e,8u,f->x[1],f->x[22]);
        }
    } else if(t==13u) {
        if(v==1u){uint16_t n=(uint16_t)(f->x[1]>64u?64u:f->x[1]);if(n)(void)segment(io,e,1u,f->x[0],n);inline_pair(e,8u,f->x[1],0u);}
        else if(v==3u){(void)segment(io,e,2u,sp,24u);uint16_t n=(uint16_t)(f->x[29]>32u?64u:f->x[29]*2u);if(n)(void)segment(io,e,3u,UINT64_C(0xa057af9ec),n);}
    } else if(t==14u) {
        if(v==1u)(void)segment(io,e,5u,f->x[0],80u);
        else if(v==2u || v==3u){(void)segment(io,e,1u,UINT64_C(0xa02b6f520),4u);(void)segment(io,e,2u,UINT64_C(0xa057b0138),1u);}
    } else if(t==15u) {
        if(v==1u || v==5u) {
            (void)segment(io,e,1u,f->x[0]+0x240u,24u);(void)segment(io,e,2u,f->x[1],4u);
            if(f->x[0]!=UINT64_C(0xa02830858))e->flags|=FX_ET_SOURCE_ERROR;
        } else if(v==10u) {
            (void)segment(io,e,1u,f->x[19]+0x240u,24u);
            inline_pair(e,10u,f->x[30],f->x[20]);
            if(f->x[19]!=UINT64_C(0xa02830858))e->flags|=FX_ET_SOURCE_ERROR;
        }
    } else if(t==16u) {
        /* Actual 19-byte AF row selection. x14 is the row index at entry;
         * x1 is the consumed source at the native memcpy call. Neither is
         * reconstructed later from the mutable global table/cache. */
        if(v==1u) {
            inline_pair(e,11u,f->x[14],f->x[21]);
            (void)segment(io,e,1u,f->x[21]+0xa0u,4u);
        } else if(v==2u) {
            inline_pair(e,12u,f->x[0],f->x[2]);
            if(f->x[2]==19u)(void)segment(io,e,2u,f->x[1],19u);
            else e->flags|=FX_ET_SOURCE_ERROR;
        } else if(v==3u)(void)segment(io,e,3u,UINT64_C(0xa03a52d70),19u);
    } else if(t==17u) {
        if(v==1u) {
            const uint8_t *p=segment(io,e,1u,f->x[0],24u);
            if(p)(void)segment(io,e,2u,fx_ptp_get64(p+16u),24u);
        } else if(v==2u || v==3u) {
            (void)segment(io,e,2u,f->x[20],24u);
            const uint8_t *p=segment(io,e,3u,f->x[20]+0x100u,8u);
            if(p)(void)segment(io,e,4u,fx_ptp_get64(p),72u);
            /* Branch-local format selector, not inferred pixel packing. */
            inline_pair(e,13u,v==2u?4u:f->x[19],0u);
        } else if(v==4u) {
            const uint8_t *p=segment(io,e,5u,f->x[0],40u);
            if(p) {
                uint64_t raw=fx_ptp_get64(p+16u),image=fx_ptp_get64(p+8u);
                if((!raw)==(!image))e->flags|=FX_ET_SOURCE_ERROR;
                else (void)segment(io,e,raw?6u:7u,raw?raw:image,32u);
            }
            (void)segment(io,e,2u,f->x[20],24u);
            (void)segment(io,e,3u,f->x[20]+0x100u,8u);
            /* No dereference of either native pixel/data pointer. */
        }
    } else if(t==18u) {
        if(v==1u) {
            const uint8_t *p=segment(io,e,1u,f->x[0],24u);
            if(p)(void)segment(io,e,2u,fx_ptp_get64(p+16u),24u);
        } else if(v==2u) {
            (void)segment(io,e,2u,f->x[19],24u);
            (void)segment(io,e,8u,sp+8u,4u);
        } else if(v==3u) {
            (void)segment(io,e,9u,f->x[2],40u);
        } else if(v==5u) {
            (void)segment(io,e,2u,f->x[19],24u);
            const uint8_t *p=segment(io,e,3u,f->x[19]+0x100u,8u);
            if(p)(void)segment(io,e,4u,fx_ptp_get64(p),72u);
        }
    } else if(t==19u) {
        if(v==1u)(void)segment(io,e,5u,f->x[0],40u);
        else if(v==2u) {
            /* Native 168-byte queue item, including consumed descriptor
             * copies. This is NOT permission to follow its pixel pointers. */
            (void)segment(io,e,10u,f->x[2],168u);
            if(f->x[0]!=UINT64_C(0xa033cede8) || f->x[1])e->flags|=FX_ET_SOURCE_ERROR;
        }
    } else if(t==20u) {
        if(v==1u)(void)segment(io,e,2u,f->x[0],24u);
        else if(v==2u || v==5u) {
            const uint8_t *p=segment(io,e,11u,f->x[0],40u);
            if(p)(void)segment(io,e,12u,fx_ptp_get64(p+8u),16u);
            (void)segment(io,e,4u,sp,72u);
        } else if(v==3u) {
            (void)segment(io,e,13u,UINT64_C(0xa03df2940),168u);
        }
    } else if(t==21u || t==22u) {
        if(v==2u && f->x[0]) {
            const uint8_t *p=segment(io,e,11u,f->x[0],40u);
            if(p)(void)segment(io,e,12u,fx_ptp_get64(p+8u),16u);
        }
    } else if(t==23u && v==2u) {
        const uint8_t *p=segment(io,e,4u,f->x[19],72u);
        if(p)(void)segment(io,e,12u,fx_ptp_get64(p+8u),16u);
        (void)segment(io,e,14u,f->x[20]+0x390u,48u);
        /* Pixel pointers inside the descriptor are values only. */
    } else if(t==25u) {
        if(v==1u)(void)segment(io,e,13u,f->x[2],168u);
        else if(v==2u) {
            const uint8_t *p=segment(io,e,15u,f->x[1],24u);
            if(p)for(uint32_t i=0;i<3u;++i) {
                uint64_t a=fx_ptp_get64(p+i*8u);
                if(a)(void)segment(io,e,(uint16_t)(16u+i),a,32u);
            }
        }
    }
}
static void observe(FxSensorTraceFrame *f,uint64_t pc,uint32_t t,uint32_t event,uint32_t kind,uint32_t depth,uint64_t capture,uint64_t token) {
    FxEventTrace *s=fx_c110_menu_event_trace();if(!fx_event_trace_begin(s,t))return;
    FxObservationIO io=current();FxEventInput e={0};e.begin=io.clock(io.context);
    e.template_id=t;e.event=event;e.kind=kind;e.pc=pc;e.sp=(uintptr_t)f+FX_SENSOR_TRACE_FRAME;e.entry_sp=e.sp+depth;
    if(t==15u && event==10u) {
        /* Original setter has just executed STR w1,[x19,#240]. Exact LR
         * identifies its actual start/stop callback, not a cache inference. */
        if(f->x[30]==UINT64_C(0x40f72f80) || f->x[30]==UINT64_C(0x40f72f68))e.entry_sp=e.sp+0x30u;
        else if(f->x[30]==UINT64_C(0x40f72fd4) || f->x[30]==UINT64_C(0x40f72ff0))e.entry_sp=e.sp+0x40u;
        else {e.entry_sp=0;e.flags|=FX_ET_SCOPE_UNKNOWN;}
    }
    e.kernel=f->reserved[1];e.cpu=f->mpidr;
    for(uint32_t i=0;i<16u;++i)e.registers[i]=f->x[registers[i]];
    kernel_identity(&io,&e);
    /* Only the two fixed native source/movie adapters follow native envelope
     * aliases. Configuration templates retain their A-only reader. */
    if(t==17u || t==18u)io.read=read_native_source;
    payload(&io,&e,f);
    if(t==15u && event==10u && f->x[19]==UINT64_C(0xa02830858) &&
       f->x[30]==UINT64_C(0x40f72f80) && (uint32_t)f->x[20]==5u && (uint32_t)f->x[1]==6u)
        e.flags|=FX_ET_REC_START;
    if(t==13u && (event==3u || event==4u))inline_pair(&e,9u,capture,token);
    e.end=io.clock(io.context);fx_event_trace_append(s,&io,&e,admitted());
}
void fx_c110_event_trace_observe(FxSensorTraceFrame *f,uint32_t site) {
    if(site>=sizeof(fx_event_sites)/sizeof(fx_event_sites[0]))return;
    fx_c110_frame_observe(f,fx_event_sites[site].pc);
    observe(f,fx_event_sites[site].pc,fx_event_sites[site].template_id,fx_event_sites[site].event,
        fx_event_sites[site].kind,fx_event_sites[site].depth,0u,0u);
}
void fx_c110_event_trace_sensor(FxSensorTraceFrame *f,uint32_t event,uint64_t capture,uint64_t token) {
    observe(f,event==3u?UINT64_C(0x413522a8):UINT64_C(0x413522ac),13u,event,FX_ET_STAGE,0x80u,capture,token);
}
void fx_c110_event_trace_cancel(uint32_t reason){fx_event_trace_cancel(fx_c110_menu_event_trace(),reason);}
void fx_c110_event_trace_step(const FxObservationIO *io,int allowed){fx_event_trace_step(fx_c110_menu_event_trace(),io,allowed);}
int fx_c110_event_trace_quiesced(void){return fx_event_trace_quiesced(fx_c110_menu_event_trace());}
uint32_t fx_c110_event_trace_request(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    *n=0;if(!admitted())return FX_PTP_CLOSED;if(bytes<4u)return FX_PTP_ARGUMENT;
    FxObservationIO io=current();uint32_t op=fx_ptp_get32(in);
    if(!op) {
        if(bytes!=4u)return FX_PTP_ARGUMENT;
        memset(out,0,224u);memcpy(out,"FXED",4u);fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,224u);
        fx_ptp_put32(out+12,7u);fx_ptp_put32(out+16,FX_ET_RECORD);fx_ptp_put32(out+20,FX_ET_BYTES);
        fx_ptp_put32(out+24,30000u);fx_ptp_put32(out+28,FX_ET_RECORDS);fx_ptp_put32(out+32,FX_ET_PAYLOAD);
        fx_ptp_put32(out+36,FX_ET_SCOPES);fx_ptp_put64(out+40,FX_C110_EVENT_MASK);
        fx_ptp_put64(out+48,io.boot);fx_ptp_put64(out+56,io.activation);memcpy(out+64,fx_c110_debug_base,32u);
        memcpy(out+96,io.build,32u);memcpy(out+128,fx_c110_debug_package,32u);
        fx_ptp_put32(out+160,1u);fx_ptp_put32(out+164,1u); /* read-only, loss invalidates association */
        *n=224u;return FX_PTP_OK;
    }
    FxEventTrace *s=fx_c110_menu_event_trace();
    if(!s && op==1u) {
        uint8_t any=0;if(bytes!=44u)return FX_PTP_ARGUMENT;for(uint32_t i=4u;i<20u;++i)any|=in[i];
        if(!any || !fx_ptp_get32(in+20) || fx_ptp_get32(in+20)>30000u || !fx_ptp_get32(in+24) || fx_ptp_get32(in+24)>FX_ET_RECORDS ||
           fx_ptp_get32(in+28)&~(FX_OBS_ALLOW_DISCONNECT|FX_ET_WAIT_REC_START) || fx_ptp_get32(in+32) ||
           !fx_ptp_get64(in+36) || fx_ptp_get64(in+36)&~FX_C110_EVENT_MASK ||
           ((fx_ptp_get32(in+28)&FX_ET_WAIT_REC_START) && !(fx_ptp_get64(in+36)&(UINT64_C(1)<<15))))return FX_PTP_ARGUMENT;
        s=fx_c110_menu_event_trace_create();
    }
    return s?fx_event_trace_request(s,&io,FX_C110_EVENT_MASK,in,bytes,out,n):op==1u?FX_PTP_UNSUPPORTED:FX_PTP_STALE;
}
const uint32_t fx_c110_event_trace_metadata[]={0x46584544u,1u,sizeof(FxEventTrace),offsetof(FxEventTrace,phase),
    offsetof(FxEventTrace,data),offsetof(FxEventTrace,accepting),offsetof(FxEventTrace,callbacks),
    offsetof(FxEventTrace,records),offsetof(FxEventTrace,scopes),FX_ET_BYTES,FX_ET_RECORD};
