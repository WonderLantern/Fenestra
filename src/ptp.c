#include "fenestra_ptp.h"
#include "fenestra_memory.h"
#include "fenestra_platform.h"

uint32_t fx_ptp_get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
uint64_t fx_ptp_get64(const uint8_t *p) { return fx_ptp_get32(p)|(uint64_t)fx_ptp_get32(p+4)<<32; }
void fx_ptp_put32(uint8_t *p,uint32_t n) {
    for(uint32_t i=0;i<4u;++i) p[i]=(uint8_t)(n>>(8u*i));
}
void fx_ptp_put64(uint8_t *p,uint64_t n) { fx_ptp_put32(p,(uint32_t)n);fx_ptp_put32(p+4,(uint32_t)(n>>32)); }
uint32_t fx_ptp_crc(const uint8_t *p,uint32_t bytes) {
    uint32_t c=UINT32_MAX;
    for(uint32_t i=0;i<bytes;++i) {
        c^=i>=28u && i<32u?0u:p[i];
        for(uint32_t b=0;b<8u;++b) c=(c>>1)^((0u-(c&1u))&0xedb88320u);
    }
    return ~c;
}
static int lock(FxPtp *p) {
    uint32_t expected=0;
    return p && atomic_compare_exchange_strong_explicit(&p->locked,&expected,1u,
        memory_order_acquire,memory_order_relaxed);
}
static void unlock(FxPtp *p) { atomic_store_explicit(&p->locked,0u,memory_order_release); }
/* Call only after the worker relinquishes RUNNING storage. */
static void clear_buffers(FxPtp *p) {
    p->request_bytes=p->response_bytes=p->completion_bytes=0;
    memset(p->request,0,sizeof(p->request));memset(p->response,0,sizeof(p->response));
    memset(p->completion,0,sizeof(p->completion));
    atomic_store_explicit(&p->completed,0u,memory_order_release);
}
static void reset(FxPtp *p,uint32_t generation) {
    p->observed_generation=generation;p->session=0;p->request_id=0;
    p->job=FX_PTP_IDLE;p->cancel_requested=0u;clear_buffers(p);
}
static uint32_t sync_gate(FxPtp *p) {
    uint32_t g=atomic_load_explicit(&p->generation,memory_order_acquire);
    /* A running backend owns its copied request. Do not reuse that slot until
     * it returns; in particular a reconnect cannot race a second execution. */
    if(g!=p->observed_generation) {
        if(p->job==FX_PTP_RUNNING) return FX_PTP_BUSY;
        reset(p,g);
    }
    return atomic_load_explicit(&p->enabled,memory_order_acquire) && g!=UINT32_MAX?
        FX_PTP_OK:FX_PTP_CLOSED;
}
void fx_ptp_init_target(FxPtp *p,const FxPtpIdentity *identity) {
    memset(p,0,sizeof(*p));atomic_init(&p->enabled,0u);
    atomic_init(&p->generation,1u);atomic_init(&p->locked,0u);atomic_init(&p->completed,0u);
    p->observed_generation=1u;
    atomic_init(&p->backend_session,0u);
    p->identity=identity;
}
void fx_ptp_init(FxPtp *p) {fx_ptp_init_target(p,0);}
static void string(uint8_t *out,const char *text,uint32_t bytes) {
    if(text)for(uint32_t i=0;i+1u<bytes && text[i];++i)out[i]=(uint8_t)text[i];
}
uint32_t fx_ptp_identity(const FxPtp *p,const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *count) {
    (void)in;
    if(!p || !out || !count)return FX_PTP_ARGUMENT;
    uint32_t capacity=*count;*count=0;
    if(bytes)return FX_PTP_ARGUMENT;
    const FxPtpIdentity *id=p->identity;
    if(!id || !(id->capabilities&(1u<<(FX_PTP_IDENTITY-1u))) ||
       !id->model || !id->revision || !id->application || !id->module_target || !id->build)return FX_PTP_UNSUPPORTED;
    if(capacity<FX_PTP_IDENTITY_BYTES)return FX_PTP_ARGUMENT;
    memset(out,0,FX_PTP_IDENTITY_BYTES);memcpy(out,"FXI1",4u);
    fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,FX_PTP_IDENTITY_BYTES);
    fx_ptp_put32(out+12,FX_PTP_VERSION);fx_ptp_put32(out+16,FX_SERVICE_ABI);
    fx_ptp_put32(out+20,FX_PLATFORM_ABI);fx_ptp_put32(out+24,1u);
    fx_ptp_put32(out+28,id->capabilities);fx_ptp_put32(out+32,id->firmware);
    fx_ptp_put32(out+36,FX_ABI);fx_ptp_put32(out+40,id->flags);
    memcpy(out+64,id->application,32u);memcpy(out+96,id->module_target,32u);memcpy(out+128,id->build,32u);
    string(out+160,id->model,48u);string(out+208,id->revision,32u);
    *count=FX_PTP_IDENTITY_BYTES;return FX_PTP_OK;
}
void fx_ptp_disconnect(FxPtp *p) {
    if(!p) return;
    uint32_t old=atomic_load_explicit(&p->generation,memory_order_relaxed);
    while(old!=UINT32_MAX && !atomic_compare_exchange_weak_explicit(&p->generation,&old,old+1u,
        memory_order_acq_rel,memory_order_relaxed)) {}
    /* Clear queued/completed storage immediately when possible. A running
     * backend owns its copies and can publish only into the old generation. */
    if(lock(p)) {
        if(p->job!=FX_PTP_RUNNING)reset(p,atomic_load_explicit(&p->generation,memory_order_acquire));
        unlock(p);
    }
}
void fx_ptp_enable(FxPtp *p,int enable) {
    if(!p) return;
    atomic_store_explicit(&p->enabled,0u,memory_order_release);
    fx_ptp_disconnect(p);
    if(enable && atomic_load_explicit(&p->generation,memory_order_acquire)!=UINT32_MAX)
        atomic_store_explicit(&p->enabled,1u,memory_order_release);
}
uint32_t fx_ptp_hello(FxPtp *p,uint8_t out[64]) {
    if(!out) return FX_PTP_ARGUMENT;
    if(!lock(p)) return FX_PTP_BUSY;
    uint32_t r=sync_gate(p);
    if(r==FX_PTP_OK && (p->job==FX_PTP_QUEUED || p->job==FX_PTP_RUNNING)) r=FX_PTP_BUSY;
    if(r==FX_PTP_OK && p->session_sequence==UINT32_MAX) r=FX_PTP_CLOSED;
    if(r==FX_PTP_OK) {
        reset(p,p->observed_generation);
        p->session=((uint64_t)p->observed_generation<<32)|++p->session_sequence;
        atomic_store_explicit(&p->backend_session,p->session,memory_order_release);
        memset(out,0,64u);memcpy(out,"FXP2",4u);
        fx_ptp_put32(out+4,FX_PTP_VERSION);fx_ptp_put64(out+8,p->session);
        const FxPtpIdentity *id=p->identity;
        fx_ptp_put32(out+16,FX_PTP_PAYLOAD);fx_ptp_put32(out+20,id?id->capabilities:0u);
        fx_ptp_put32(out+24,id?id->firmware:0u);fx_ptp_put32(out+28,1u); /* one retained job */
        /* A new handshake has no running Linux agent. Native RTOS RAM writes
         * and explicit Linux bootstrap are privileged debugging capabilities. */
        fx_ptp_put32(out+36,id?(id->flags&(1u|FX_PTP_FLAG_CONTROL)):0u);
        string(out+40,id?id->model:"Fenestra",24u);
    }
    unlock(p);return r;
}
uint32_t fx_ptp_submit(FxPtp *p,const uint8_t *in,uint32_t bytes) {
    if(!in || bytes<FX_PTP_HEADER || bytes>FX_PTP_FRAME || memcmp(in,"FXP2",4u) ||
       in[4]!=FX_PTP_VERSION || in[5] || fx_ptp_get32(in+20)!=bytes-FX_PTP_HEADER ||
       fx_ptp_get32(in+24)!=0u || !fx_ptp_get32(in+16)) return FX_PTP_ARGUMENT;
    if(fx_ptp_crc(in,bytes)!=fx_ptp_get32(in+28)) return FX_PTP_INTEGRITY;
    if(!lock(p)) return FX_PTP_BUSY;
    uint32_t r=sync_gate(p),id=fx_ptp_get32(in+16);
    if(r==FX_PTP_OK && (!p->session || fx_ptp_get64(in+8)!=p->session)) r=FX_PTP_STALE;
    if(r==FX_PTP_OK && id==p->request_id) {
        r=p->job!=FX_PTP_CANCELLED && p->job!=FX_PTP_RELEASED &&
            bytes==p->request_bytes && !memcmp(in,p->request,bytes)?FX_PTP_OK:FX_PTP_STALE;
    } else if(r==FX_PTP_OK) {
        if(id<p->request_id) r=FX_PTP_STALE;
        else if(p->job==FX_PTP_QUEUED || p->job==FX_PTP_RUNNING) r=FX_PTP_BUSY;
        else {
            memcpy(p->request,in,bytes);p->request_bytes=bytes;p->request_id=id;
            p->response_bytes=0;p->cancel_requested=0u;p->job=FX_PTP_QUEUED;
        }
    }
    if(r!=FX_PTP_OK && p->rejected!=UINT32_MAX) ++p->rejected;
    unlock(p);return r;
}
uint32_t fx_ptp_fetch(FxPtp *p,uint64_t session,uint32_t id,uint32_t offset,
                      uint8_t *out,uint32_t capacity,uint32_t *bytes) {
    if(bytes) *bytes=0;
    if(!out || !bytes || !capacity || capacity>FX_PTP_FRAME) return FX_PTP_ARGUMENT;
    if(!lock(p)) return FX_PTP_BUSY;
    uint32_t r=sync_gate(p);
    if(r==FX_PTP_OK && (!session || session!=p->session || !id || id!=p->request_id)) r=FX_PTP_STALE;
    if(r==FX_PTP_OK && (p->job==FX_PTP_CANCELLED || p->job==FX_PTP_RELEASED))r=FX_PTP_STALE;
    if(r==FX_PTP_OK && p->job!=FX_PTP_DONE) r=FX_PTP_BUSY;
    if(r==FX_PTP_OK && offset>p->response_bytes) r=FX_PTP_ARGUMENT;
    if(r==FX_PTP_OK) {
        uint32_t n=p->response_bytes-offset;if(n>capacity) n=capacity;
        memcpy(out,p->response+offset,n);*bytes=n;
    }
    unlock(p);return r;
}
/* Runs in the USB task, never behind a queued/running backend. Cancellation
 * prevents queued execution; for RUNNING it only discards the eventual result.
 * Tombstones retain request_id so a lost acknowledgement cannot replay work. */
uint32_t fx_ptp_control(FxPtp *p,uint64_t session,uint32_t id,uint32_t action,
                        uint8_t out[FX_PTP_CONTROL_BYTES]) {
    if(!out || action>FX_PTP_JOB_RELEASE)return FX_PTP_ARGUMENT;
    if(!lock(p))return FX_PTP_BUSY;
    uint32_t r=sync_gate(p);
    if(r==FX_PTP_OK && (!session || session!=p->session || !id || id!=p->request_id))r=FX_PTP_STALE;
    if(r==FX_PTP_OK && action==FX_PTP_JOB_CANCEL) {
        if(p->job!=FX_PTP_RELEASED) {
            p->cancel_requested=1u;
            if(p->job!=FX_PTP_RUNNING){clear_buffers(p);p->job=FX_PTP_CANCELLED;}
        }
    } else if(r==FX_PTP_OK && action==FX_PTP_JOB_RELEASE) {
        if(p->job==FX_PTP_RUNNING || p->job==FX_PTP_QUEUED)r=FX_PTP_BUSY;
        else {clear_buffers(p);p->job=FX_PTP_RELEASED;}
    }
    if(r==FX_PTP_OK) {
        memset(out,0,FX_PTP_CONTROL_BYTES);memcpy(out,"FXC1",4u);
        fx_ptp_put32(out+4,1u);fx_ptp_put32(out+8,id);fx_ptp_put32(out+12,p->job);
        fx_ptp_put32(out+16,p->cancel_requested);fx_ptp_put32(out+20,p->response_bytes);
        fx_ptp_put64(out+24,session);
    }
    unlock(p);return r;
}
static uint32_t publish(FxPtp *p) {
    uint32_t r=FX_PTP_OK;
    if(p->completion_generation!=atomic_load_explicit(&p->generation,memory_order_acquire) ||
       !atomic_load_explicit(&p->enabled,memory_order_acquire)) {
        if(p->discarded!=UINT32_MAX) ++p->discarded;
        reset(p,atomic_load_explicit(&p->generation,memory_order_acquire));
        r=FX_PTP_CLOSED;
    } else if(p->cancel_requested) {
        if(p->discarded!=UINT32_MAX)++p->discarded;
        clear_buffers(p);p->job=FX_PTP_CANCELLED;
    } else {
        memcpy(p->response,p->completion,p->completion_bytes);
        p->response_bytes=p->completion_bytes;p->job=FX_PTP_DONE;
        memset(p->completion,0,sizeof(p->completion));p->completion_bytes=0u;
    }
    atomic_store_explicit(&p->completed,0u,memory_order_release);
    return r;
}
uint32_t fx_ptp_poll(FxPtp *p,FxPtpBackend backend,void *context) {
    if(!backend) return FX_PTP_ARGUMENT;
    if(!lock(p)) return FX_PTP_BUSY;
    if(atomic_load_explicit(&p->completed,memory_order_acquire)) {
        uint32_t result=publish(p);unlock(p);return result;
    }
    uint32_t r=sync_gate(p);
    if(r!=FX_PTP_OK || p->job!=FX_PTP_QUEUED) { unlock(p);return r; }
    uint8_t input[FX_PTP_FRAME],output[FX_PTP_FRAME];
    uint32_t generation=p->observed_generation,bytes=p->request_bytes,n=FX_PTP_PAYLOAD;
    memcpy(input,p->request,bytes);p->job=FX_PTP_RUNNING;
    if(p->executions!=UINT32_MAX) ++p->executions;
    unlock(p);
    memset(output,0,sizeof(output));
    uint16_t op=(uint16_t)((uint16_t)input[6]|(uint16_t)((uint16_t)input[7]<<8));
    r=op==FX_PTP_IDENTITY?fx_ptp_identity(p,input+FX_PTP_HEADER,bytes-FX_PTP_HEADER,output+FX_PTP_HEADER,&n):
        backend(context,op,input+FX_PTP_HEADER,bytes-FX_PTP_HEADER,output+FX_PTP_HEADER,&n);
    if(n>FX_PTP_PAYLOAD) { r=FX_PTP_INTEGRITY;n=0; }
    if(r!=FX_PTP_OK) n=0;
    memcpy(output,input,FX_PTP_HEADER);fx_ptp_put32(output+20,n);
    fx_ptp_put32(output+24,0x80000000u|r);
    fx_ptp_put32(output+28,fx_ptp_crc(output,FX_PTP_HEADER+n));
    /* Never spin: a preempted lower-priority USB owner must be able to run.
     * Completion survives contention and the next poll publishes it once. */
    memcpy(p->completion,output,FX_PTP_HEADER+n);
    p->completion_bytes=FX_PTP_HEADER+n;p->completion_generation=generation;
    atomic_store_explicit(&p->completed,1u,memory_order_release);
    if(!lock(p)) return FX_PTP_BUSY;
    r=publish(p);
    unlock(p);return r;
}
