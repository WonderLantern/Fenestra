/* Normal original Linux IPC, activated only by a local USB Debug gate.
 * No boot hook, maintenance mode or external shell tooling. Original receive
 * dispatcher is always called; only its otherwise discarded text is copied.
 */
#include "fenestra_native_linux.h"
#include "fenestra_native_rtos.h"
#include "fenestra_ptp.h"
#include "fenestra_memory.h"
#define POOL (fx_native_linux_profile.pool)
#define POOL_BYTES (fx_native_linux_profile.pool_bytes)
#define SHARED 8192u
void fx_linux_account(FxLinuxBridge *bridge,FxAllocationObserver observer) {
    if(bridge)bridge->observer=observer;
}
void fx_linux_account_packets(FxLinuxBridge *bridge,FxIndexMemory memory,FxAllocationObserver requests,FxAllocationObserver metadata) {
    if(bridge && !bridge->transfers.configured)(void)fx_transfers_init(&bridge->transfers,memory,requests,metadata,0u);
}
/* Only the typed bootstrap path can prove that no process has been launched.
 * Arbitrary legacy shell commands conservatively take ownership as well. */
static char *append(char *p,const char *s) { while(*s)*p++=*s++;return p; }
static char *hex(char *p,uint64_t value,uint32_t digits) {
    for(uint32_t i=digits;i;--i)*p++="0123456789abcdef"[(value>>((i-1u)*4u))&15u];
    return p;
}
static uint32_t bootstrap(FxLinuxBridge *s,const uint8_t *in,uint32_t bytes,char *out) {
    if(bytes<8u)return 0;
    uint32_t stage=fx_ptp_get32(in),serial=fx_ptp_get32(in+4);in+=8;bytes-=8;
    if(stage>3u || !serial || bytes>512u || (stage!=1u && bytes) ||
       (stage==1u && (!bytes || bytes%4u)))return 0;
    for(uint32_t i=0;i<bytes;++i) {
        uint8_t c=in[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'||c=='='))return 0;
    }
    char directory[40],*q=append(directory,"/tmp/fenestra-");q=hex(q,s->nonce,16);*q=0;
    char *p=append(out,"{\n");
    /* Every stage checks the mount; a skipped/failed init cannot redirect an
     * upload to rootfs. No client-supplied path or shell syntax is accepted. */
    p=append(p,"ok=0; while read a b c d; do [ \"$b:$c\" = /tmp:tmpfs ] && ok=1; done < /proc/mounts; [ \"$ok\" = 1 ] && [ ! -L /tmp ] && ");
    if(stage==0u) {
        p=append(p,"base64 -d </dev/null >/dev/null && (gzip -c </dev/null | gzip -dc >/dev/null) && (umask 077; mkdir ");
        p=append(p,directory);p=append(p,")");
    } else {
        /* No client-supplied directory; stage1 is append-only data and
         * stage2 just decodes. Both always run in the foreground. */
        p=append(p,"[ ! -L ");p=append(p,directory);p=append(p," ] && cd ");p=append(p,directory);
        p=append(p," && ");
        if(stage==1u) {
            p=append(p,"[ ! -L agent.b64 ] && printf '%s' '");
            memcpy(p,in,bytes);p+=bytes;p=append(p,"' >> agent.b64");
        } else if(stage==2u) p=append(p,"[ ! -L agent ] && (base64 -d agent.b64 | gzip -dc > agent)");
        else {
            p=append(p,"chmod 700 agent && ./agent ");p=hex(p,(uintptr_t)s->shared,16);
            *p++=' ';p=hex(p,s->nonce,16);p=append(p," ");p=append(p,fx_native_linux_profile.bootstrap_token);p=append(p," </dev/null >/dev/null 2>&1 &");
        }
    }
    p=append(p,"\n}; r=$?; printf \"FXB");p=hex(p,s->nonce,16);p=hex(p,serial,8);
    p=append(p,"=%d\\n\" \"$r\"");
    return (uint32_t)(p-out);
}
static int take(FxLinuxBridge *s) {
    uint32_t expected=0;
    return atomic_compare_exchange_strong_explicit(&s->log_lock,&expected,1u,memory_order_acquire,memory_order_relaxed);
}
static void give(FxLinuxBridge *s) { atomic_store_explicit(&s->log_lock,0u,memory_order_release); }
static void clean(uint8_t *p,uint32_t n) {
    for(uintptr_t at=(uintptr_t)p&~UINT64_C(63);at<(uintptr_t)p+n;at+=64u)
        __asm__ volatile("dc cvac, %0"::"r"(at):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
static void invalidate(uint8_t *p,uint32_t n) {
    for(uintptr_t at=(uintptr_t)p&~UINT64_C(63);at<(uintptr_t)p+n;at+=64u)
        __asm__ volatile("dc ivac, %0"::"r"(at):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
static uint32_t receive(uint32_t opcode,const uint8_t *payload) {
    FxLinuxBridge *s=fx_native_linux_profile.state();
    FxTransferClaim released={0};
    /* Original dispatch masks the low 16 bits. Op2 completes a command; op4
     * frees its packet. Claim before forwarding so an immediate allocator
     * reuse cannot retire the next generation's charge. No metadata allocation
     * or native wait takes place on the receiver path. */
    if(s && (opcode&0xffffu)==fx_native_linux_profile.release_opcode) {
        uint32_t offset=fx_ptp_get32(payload);
        if((offset || fx_native_linux_profile.release_zero_offset) && offset<=POOL_BYTES-64u)
            released=fx_transfers_claim(&s->transfers,(uintptr_t)(POOL+offset));
    }
    if(s && opcode==1u && atomic_load_explicit(&s->command_running,memory_order_acquire)) {
        uint32_t offset=fx_ptp_get32(payload);
        if(offset<=POOL_BYTES-96u && take(s)) {
            const uint8_t *text=(const uint8_t *)(uintptr_t)(POOL+offset);
            invalidate((uint8_t *)(uintptr_t)text,96u);
            /* LaMainApp prefixes EACH 88-byte fragment, including fragments
             * that split our marker. Strip only that per-packet prefix. */
            uint32_t first=memcmp(text,"[Linux]",7u)==0?7u:0u;
            for(uint32_t i=first;i<96u && text[i];++i)s->log[(s->log_end++)%FX_USB_LOG]=text[i];
            give(s);
        } else atomic_fetch_add_explicit(&s->dropped,1u,memory_order_relaxed);
    }
    if(s && opcode==2u) {
        atomic_fetch_add_explicit(&s->command_completions,1u,memory_order_relaxed);
        atomic_store_explicit(&s->command_running,0u,memory_order_release);
    }
    uint32_t result=fx_native_linux_profile.receive(opcode,payload);
    if(s)fx_transfers_complete(&s->transfers,&released);
    return result;
}
static int install(FxLinuxBridge *s) {
    if(!*fx_native_linux_profile.ready ||
       *fx_native_linux_profile.pool_address!=POOL ||
       *fx_native_linux_profile.pool_size!=POOL_BYTES)return 0;
    _Atomic uintptr_t *callback=fx_native_linux_profile.callback;
    uintptr_t expected=(uintptr_t)fx_native_linux_profile.receive;
    if(!atomic_compare_exchange_strong_explicit(callback,&expected,(uintptr_t)receive,
            memory_order_acq_rel,memory_order_acquire) && expected!=(uintptr_t)receive)return 0;
    s->hooked=1u;return 1;
}
/* The stock send wrapper retries forever. Try its native recursive spinlock
 * once, enqueue once with wait=0, then notify while the lock is still owned.
 * Nested original queue calls cannot wait on another core in this window. */
static int send_command(const uint8_t *command,uint32_t bytes) {
    uintptr_t queue=*fx_native_linux_profile.queue;
    if(queue<UINT64_C(0xa00000000) || queue>UINT64_C(0xa40000000)-16u)return 0;
    uintptr_t block=*(volatile uintptr_t *)(queue+8u);
    if(block<POOL || block>=fx_native_linux_profile.queue_block_end)return 0;
    void *lock=(void *)(block+0x30u);
    uint8_t *input=fx_native_linux_profile.allocate(bytes+4u);
    if(!input)return 0;
    FxLinuxBridge *s=fx_native_linux_profile.state();
    FxTransferClaim owned=fx_transfers_begin(s?&s->transfers:0,(uintptr_t)input,bytes+4u);
    memcpy(input,"sh ",3u);memcpy(input+3u,command,bytes);input[bytes+3u]=0;
    int32_t offset=fx_native_linux_profile.offset(input);
    if(offset<0 || fx_native_linux_profile.try_lock(lock)!=0) {
        fx_native_linux_profile.release(input);if(s)fx_transfers_cancel(&s->transfers,&owned);return 0;
    }
    uint8_t message[36];memset(message,0,sizeof(message));
    /* No reply requested: sequence is not entered in the native wait queue. */
    message[6]=3u;fx_ptp_put32(message+8,(uint32_t)offset);clean(input,bytes+4u);
    int32_t result=fx_native_linux_profile.enqueue(queue,message,0u);
    if(result==0)(void)fx_native_linux_profile.notify(queue);
    (void)fx_native_linux_profile.unlock(lock);
    if(result!=0){fx_native_linux_profile.release(input);if(s)fx_transfers_cancel(&s->transfers,&owned);}
    /* Once queued, LaMainApp owns input and frees it through original op4. */
    return result==0;
}
void fx_linux_tick(uint64_t session,uint32_t generation,int enabled) {
    FxLinuxBridge *s=fx_native_linux_profile.state();if(!s)return;
    if(s->shared && (!enabled || s->generation!=generation || s->session!=session)) {
        atomic_store_explicit((_Atomic uint32_t *)(s->shared+72),1u,memory_order_release);
        clean(s->shared+64,64u);
    }
    if(!enabled)return;
    if(!s->shared){s->generation=generation;s->session=session;return;}
    if(s->generation!=generation || s->session!=session)return;
    atomic_store_explicit((_Atomic uint64_t *)(s->shared+64),++s->heartbeat,memory_order_release);
    clean(s->shared+64,64u);
}
void fx_linux_revoke(void) {
    fx_rtos_revoke();
    FxLinuxBridge *s=fx_native_linux_profile.state();if(!s || !s->shared)return;
    atomic_store_explicit((_Atomic uint32_t *)(s->shared+72),1u,memory_order_release);
    clean(s->shared+64,64u);
}
uint32_t fx_linux_ready(void) {
    FxLinuxBridge *s=fx_native_linux_profile.state();if(!s || !s->shared)return 0;
    invalidate(s->shared+4096,64u);
    return atomic_load_explicit((_Atomic uint32_t *)(s->shared+4128),memory_order_acquire)==1u &&
        atomic_load_explicit((_Atomic uint32_t *)(s->shared+72),memory_order_acquire)==0u;
}
uint32_t fx_linux_request(const uint8_t *in,uint32_t bytes,uint8_t *out,uint32_t *n) {
    FxLinuxBridge *s=fx_native_linux_profile.state();if(!s || bytes<4u)return FX_PTP_ARGUMENT;
    uint32_t op=fx_ptp_get32(in);in+=4u;bytes-=4u;
    memset(out,0,FX_PTP_PAYLOAD);
    if(op==0u) {
        if(bytes)return FX_PTP_ARGUMENT;
        memcpy(out,"FXB1",4u);fx_ptp_put32(out+4,2u);fx_ptp_put32(out+8,s->hooked);
        fx_ptp_put32(out+12,fx_linux_ready());fx_ptp_put64(out+16,(uintptr_t)s->shared);
        fx_ptp_put64(out+24,s->nonce);fx_ptp_put32(out+32,atomic_load(&s->command_running));
        fx_ptp_put32(out+36,atomic_load(&s->command_completions));fx_ptp_put32(out+40,atomic_load(&s->dropped));
        fx_ptp_put32(out+44,s->sequence);*n=48u;return FX_PTP_OK;
    }
    if(op==1u) {
        if(bytes!=8u || !fx_ptp_get64(in))return FX_PTP_ARGUMENT;
        uint64_t nonce=fx_ptp_get64(in);
        if(!install(s))return FX_PTP_UNSUPPORTED;
        if(s->shared) {
            invalidate(s->shared+4096,64u);
            if(s->nonce==nonce && !atomic_load((_Atomic uint32_t *)(s->shared+72))){*n=0;return FX_PTP_OK;}
            /* Never recycle memory while a former Linux process may map it. */
            if(!fx_linux_quiesced())return FX_PTP_BUSY;
        } else {
            /* A failed native span remains owned, but never gets replaced by
             * another allocation while its original ownership is uncertain. */
            if(s->allocation)return FX_PTP_INTEGRITY;
            s->allocation=fx_native_linux_profile.allocate(SHARED+4096u);
            if(!s->allocation)return FX_PTP_BUSY;
            fx_allocation_observe(s->observer,SHARED+4096u,FX_ALLOC_REQUESTED);
            fx_allocation_observe(s->observer,SHARED+4096u,FX_ALLOC_PUBLISHED);
            uintptr_t base=((uintptr_t)s->allocation+4095u)&~UINT64_C(4095);
            if(base<POOL || base>POOL+POOL_BYTES-SHARED)return FX_PTP_INTEGRITY;
            s->shared=(uint8_t *)base;
        }
        memset(s->shared,0,SHARED);memcpy(s->shared,"FXL3",4u);fx_ptp_put32(s->shared+4,SHARED);
        atomic_store(&s->launch_possible,0u);
        fx_ptp_put64(s->shared+8,nonce);s->nonce=nonce;s->sequence=0;s->heartbeat=1;
        FxPtp *p=fx_native_linux_profile.ptp();s->generation=atomic_load(&p->generation);s->session=atomic_load(&p->backend_session);
        fx_ptp_put64(s->shared+64,s->heartbeat);clean(s->shared,SHARED);*n=0;return FX_PTP_OK;
    }
    if(op==2u || op==7u) { /* Original sh queue, raw or typed bootstrap. */
        if(!s->hooked || !s->shared)return FX_PTP_CLOSED;
        if(atomic_load((_Atomic uint32_t *)(s->shared+72)))return FX_PTP_CLOSED;
        char command[1024];uint32_t may_launch=op==2u || (bytes>=4u && fx_ptp_get32(in)==3u);
        if(op==7u) {
            if(atomic_load(&s->launch_possible))return FX_PTP_CLOSED;
            bytes=bootstrap(s,in,bytes,command);in=(const uint8_t *)command;
        }
        if(!bytes || bytes>1000u || (op==2u && bytes>900u))return FX_PTP_ARGUMENT;
        for(uint32_t i=0;i<bytes;++i)if(!in[i])return FX_PTP_ARGUMENT;
        if(atomic_exchange_explicit(&s->command_running,1u,memory_order_acq_rel))return FX_PTP_BUSY;
        if(atomic_load((_Atomic uint32_t *)(s->shared+72))){atomic_store(&s->command_running,0u);return FX_PTP_CLOSED;}
        if(!take(s)){atomic_store(&s->command_running,0u);return FX_PTP_BUSY;}
        s->log_end=0;atomic_store(&s->dropped,0u);give(s);
        uint32_t previous=atomic_load(&s->launch_possible);
        if(may_launch)atomic_store_explicit(&s->launch_possible,1u,memory_order_release);
        if(!send_command(in,bytes)){
            atomic_store(&s->launch_possible,previous);atomic_store(&s->command_running,0u);return FX_PTP_BUSY;
        }
        *n=0;return FX_PTP_OK;
    }
    if(op==3u) {
        if(bytes!=8u)return FX_PTP_ARGUMENT;
        if(!take(s))return FX_PTP_BUSY;
        uint64_t offset=fx_ptp_get64(in),base=s->log_end>FX_USB_LOG?s->log_end-FX_USB_LOG:0;
        if(offset<base || offset>s->log_end){give(s);return FX_PTP_STALE;}
        uint32_t count=(uint32_t)(s->log_end-offset);if(count>992u)count=992u;
        fx_ptp_put64(out,base);fx_ptp_put64(out+8,s->log_end);fx_ptp_put32(out+16,atomic_load(&s->command_running));
        fx_ptp_put32(out+20,atomic_load(&s->dropped));fx_ptp_put32(out+24,count);
        for(uint32_t i=0;i<count;++i)out[32u+i]=s->log[(offset+i)%FX_USB_LOG];
        give(s);*n=32u+count;return FX_PTP_OK;
    }
    if(op==4u) {
        if(bytes<4u || bytes>1020u || !fx_linux_ready())return FX_PTP_CLOSED;
        invalidate(s->shared+2048,64u);
        if(atomic_load((_Atomic uint32_t *)(s->shared+2048))!=s->sequence)return FX_PTP_BUSY;
        if(s->sequence==UINT32_MAX)return FX_PTP_CLOSED;
        memcpy(s->shared+136,in,bytes);fx_ptp_put32(s->shared+132,bytes);
        clean(s->shared+128,1088u);
        atomic_store_explicit((_Atomic uint32_t *)(s->shared+128),++s->sequence,memory_order_release);
        clean(s->shared+128,64u);fx_ptp_put32(out,s->sequence);*n=4u;return FX_PTP_OK;
    }
    if(op==5u) {
        if(bytes!=4u || !s->shared || fx_ptp_get32(in)!=s->sequence || !s->sequence)return FX_PTP_STALE;
        invalidate(s->shared+2048,1088u);
        if(atomic_load_explicit((_Atomic uint32_t *)(s->shared+2048),memory_order_acquire)!=s->sequence)return FX_PTP_BUSY;
        uint32_t length=fx_ptp_get32(s->shared+2052);if(length<8u || length>1024u)return FX_PTP_INTEGRITY;
        memcpy(out,s->shared+2056,length);*n=length;return FX_PTP_OK;
    }
    if(op==6u) {
        if(bytes)return FX_PTP_ARGUMENT;
        if(s->shared){atomic_store((_Atomic uint32_t *)(s->shared+72),1u);clean(s->shared+64,64u);}
        *n=0;return FX_PTP_OK;
    }
    return FX_PTP_UNSUPPORTED;
}

int fx_linux_quiesced(void) {
    FxLinuxBridge *s=fx_native_linux_profile.state();if(!s)return 1;
    if(atomic_load_explicit(&s->command_running,memory_order_acquire))return 0;
    if(!s->shared)return 1;
    invalidate(s->shared+4096,64u);
    uint32_t agent=atomic_load_explicit((_Atomic uint32_t *)(s->shared+4128),memory_order_acquire);
    return agent==2u || (agent==0u && !atomic_load_explicit(&s->launch_possible,memory_order_acquire));
}
