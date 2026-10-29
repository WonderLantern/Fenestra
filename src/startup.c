/* Card-only startup journal. Absence is accepted only after a successful
 * native filesystem query. Configuration uses two independently valid slots.
 * This policy never treats failed IO or an invalid flag as a clean shutdown. */
#include "fenestra_startup.h"
#include "fenestra_memory.h"
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void p32(uint8_t *p,uint32_t v) { for(uint32_t i=0;i<4u;++i)p[i]=(uint8_t)(v>>(i*8u)); }
static uint32_t crc(const uint8_t *p) {
    uint32_t c=UINT32_MAX;
    for(uint32_t i=0;i<60u;++i){c^=p[i];for(uint32_t b=0;b<8u;++b)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
    return ~c;
}
static int valid(const uint8_t *p,uint32_t bytes,int flag,uint32_t identity) {
    if(bytes!=FX_STARTUP_RECORD || memcmp(p,flag?"FXBOOT1\0":"FXCONF1\0",8u) ||
       u32(p+8)!=identity || u32(p+60)!=crc(p))return 0;
    for(uint32_t i=32u;i<60u;++i)if(p[i])return 0;
    if(flag)return !u32(p+20) && !(u32(p+16)&~3u) && (u32(p+24)|u32(p+28))!=0u;
    return u32(p+12)!=0u && !(u32(p+16)&~3u) && !u32(p+20) && !u32(p+24) && !u32(p+28);
}
static void record(uint8_t out[64],int flag,uint32_t generation,uint32_t desired,uint64_t token,uint32_t identity) {
    memset(out,0,64u);memcpy(out,flag?"FXBOOT1\0":"FXCONF1\0",8u);
    p32(out+8,identity);p32(out+12,generation);p32(out+16,desired);
    p32(out+24,(uint32_t)token);p32(out+28,(uint32_t)(token>>32));p32(out+60,crc(out));
}
static int io_valid(const FxStartupIO *io) { return io && io->read && io->create && io->remove; }
static FxResult failure(FxStartup *s,FxResult r) { s->error=r;return r; }
void fx_startup_init(FxStartup *s,int automatic) {
    fx_startup_init_profile(s,automatic,110u);
}
void fx_startup_init_profile(FxStartup *s,int automatic,uint32_t identity) {
    memset(s,0,sizeof(*s));s->desired=FX_STARTUP_DEMO|(automatic?FX_STARTUP_AUTO:0u);s->slot=UINT32_MAX;
    s->record_identity=identity;
}
FxResult fx_startup_inspect(FxStartup *s,const FxStartupIO *io) {
    if(!s || !io_valid(io) || (s->phase!=FX_STARTUP_UNCHECKED && s->phase!=FX_STARTUP_IO_ERROR))return FX_STATE;
    uint8_t rows[2][65],flag[65];uint32_t sizes[2]={0,0},size=0;FxResult r;
    r=io->read(io->context,FX_STARTUP_FLAG,flag,65u,&size);
    int flag_present=r==FX_OK;
    if(r!=FX_OK && r!=FX_EMPTY){s->phase=FX_STARTUP_IO_ERROR;return failure(s,r);}
    s->flag_known=flag_present?(uint32_t)valid(flag,size,1,s->record_identity):0u;
    if(s->flag_known)memcpy(s->owned_record,flag,64u);
    uint32_t selected=UINT32_MAX;int invalid=0;
    for(uint32_t i=0;i<2u;++i) {
        r=io->read(io->context,FX_STARTUP_CFG0+i,rows[i],65u,&sizes[i]);
        if(r==FX_EMPTY)continue;
        if(r!=FX_OK){s->phase=flag_present?FX_STARTUP_ABNORMAL:FX_STARTUP_IO_ERROR;return failure(s,flag_present?FX_STATE:r);}
        if(!valid(rows[i],sizes[i],0,s->record_identity)){invalid=1;continue;}
        uint32_t generation=u32(rows[i]+12);
        if(selected!=UINT32_MAX && generation==u32(rows[selected]+12) && memcmp(rows[i],rows[selected],64u)) {
            s->phase=flag_present?FX_STARTUP_ABNORMAL:FX_STARTUP_IO_ERROR;return failure(s,flag_present?FX_STATE:FX_INTEGRITY);
        }
        if(selected==UINT32_MAX || generation>u32(rows[selected]+12))selected=i;
    }
    if(selected!=UINT32_MAX){s->desired=u32(rows[selected]+16);s->generation=u32(rows[selected]+12);s->slot=selected;}
    if(flag_present) {
        s->phase=FX_STARTUP_ABNORMAL;
        return failure(s,FX_STATE);
    }
    if(selected==UINT32_MAX && invalid) {
        s->phase=FX_STARTUP_IO_ERROR;return failure(s,FX_INTEGRITY);
    }
    s->phase=FX_STARTUP_READY;s->error=FX_OK;return FX_OK;
}
FxResult fx_startup_configure(FxStartup *s,const FxStartupIO *io,uint32_t desired) {
    if(!s || !io_valid(io) || desired&~3u || s->phase==FX_STARTUP_UNCHECKED ||
       s->phase==FX_STARTUP_IO_ERROR || s->phase==FX_STARTUP_FAULT || s->phase==FX_STARTUP_STOPPED)return FX_STATE;
    if(desired==s->desired)return FX_OK;
    if(s->generation==UINT32_MAX)return failure(s,FX_BOUNDS);
    uint32_t slot=s->slot==0u?1u:0u,bytes=0;uint8_t old[65],next[64];
    FxResult r=io->read(io->context,FX_STARTUP_CFG0+slot,old,65u,&bytes);
    if(r==FX_OK) {
        /* Never overwrite a foreign or corrupt record. Preserve it for review. */
        if(!valid(old,bytes,0,s->record_identity))return failure(s,FX_INTEGRITY);
        r=io->remove(io->context,FX_STARTUP_CFG0+slot);if(r!=FX_OK)return failure(s,r);
    } else if(r!=FX_EMPTY)return failure(s,r);
    record(next,0,s->generation+1u,desired,0,s->record_identity);
    r=io->create(io->context,FX_STARTUP_CFG0+slot,next,64u);if(r!=FX_OK)return failure(s,r);
    r=io->read(io->context,FX_STARTUP_CFG0+slot,old,65u,&bytes);
    if(r!=FX_OK || bytes!=64u || memcmp(old,next,64u))return failure(s,r==FX_OK?FX_INTEGRITY:r);
    s->desired=desired;s->slot=slot;++s->generation;s->error=FX_OK;return FX_OK;
}
FxResult fx_startup_arm(FxStartup *s,const FxStartupIO *io,uint64_t token) {
    if(!s || !io_valid(io) || s->phase!=FX_STARTUP_READY || !token)return FX_STATE;
    record(s->owned_record,1,s->generation,s->desired,token,s->record_identity);
    FxResult r=io->create(io->context,FX_STARTUP_FLAG,s->owned_record,64u);
    if(r!=FX_OK){s->phase=FX_STARTUP_IO_ERROR;return failure(s,r);}
    uint8_t readback[65];uint32_t bytes=0;
    r=io->read(io->context,FX_STARTUP_FLAG,readback,65u,&bytes);
    if(r!=FX_OK || bytes!=64u || memcmp(readback,s->owned_record,64u)) {
        s->phase=FX_STARTUP_FAULT;return failure(s,r==FX_OK?FX_INTEGRITY:r);
    }
    s->token=token;s->owned=1u;s->activation=s->desired;s->phase=FX_STARTUP_ARMED;
    s->error=FX_OK;return FX_OK;
}
void fx_startup_running(FxStartup *s) { if(s && s->phase==FX_STARTUP_ARMED)s->phase=FX_STARTUP_RUNNING; }
void fx_startup_fault(FxStartup *s,FxResult r) { if(s){s->phase=FX_STARTUP_FAULT;s->error=r;} }
FxResult fx_startup_clean_shutdown(FxStartup *s,const FxStartupIO *io,int quiesced) {
    if(!s || !io_valid(io) || !quiesced || !s->owned || s->phase!=FX_STARTUP_RUNNING)return FX_STATE;
    uint8_t readback[65];uint32_t bytes=0;FxResult r=io->read(io->context,FX_STARTUP_FLAG,readback,65u,&bytes);
    if(r!=FX_OK || bytes!=64u || memcmp(readback,s->owned_record,64u))return failure(s,r==FX_OK?FX_INTEGRITY:r);
    r=io->remove(io->context,FX_STARTUP_FLAG);if(r!=FX_OK)return failure(s,r);
    r=io->read(io->context,FX_STARTUP_FLAG,readback,65u,&bytes);
    if(r!=FX_EMPTY)return failure(s,r==FX_OK?FX_INTEGRITY:r);
    s->owned=0;s->phase=FX_STARTUP_STOPPED;s->error=FX_OK;return FX_OK;
}
FxResult fx_startup_clear_abnormal(FxStartup *s,const FxStartupIO *io) {
    if(!s || !io_valid(io) || s->phase!=FX_STARTUP_ABNORMAL || !s->flag_known)return FX_STATE;
    uint8_t data[65];uint32_t size=0;FxResult r=io->read(io->context,FX_STARTUP_FLAG,data,65u,&size);
    if(r!=FX_OK || !valid(data,size,1,s->record_identity) || memcmp(data,s->owned_record,64u))return failure(s,r==FX_OK?FX_INTEGRITY:r);
    r=io->remove(io->context,FX_STARTUP_FLAG);if(r!=FX_OK)return failure(s,r);
    r=io->read(io->context,FX_STARTUP_FLAG,data,65u,&size);
    if(r!=FX_EMPTY)return failure(s,r==FX_OK?FX_INTEGRITY:r);
    s->flag_known=0;s->phase=FX_STARTUP_READY;s->error=FX_OK;return FX_OK;
}
