#include "fenestra_startup_files.h"
static FxResult read_record(void *context,uint32_t id,uint8_t *data,uint32_t capacity,uint32_t *bytes) {
    FxStartupFiles *s=context;
    if(!bytes)return FX_ARGUMENT;
    *bytes=0;
    if(id>FX_STARTUP_CFG1 || !data || capacity!=65u)return FX_ARGUMENT;
    const FxStartupFileProfile *p=s->profile;
    int32_t fd=p->open(p->paths[id],0u,0u);
    FxCapacityClaim claim=fx_file_observe_open(s->observer,fd);
    atomic_store(&s->native_error,fd<0?(uint32_t)fd:0u);
    if((uint32_t)fd==p->missing)return FX_EMPTY;
    if(fd<0)return FX_IO;
    FxResult result=FX_OK;
    /* Short successful reads are legal, including on the original backend. */
    while(*bytes<capacity) {
        uint32_t done=0,room=capacity-*bytes;
        uint32_t r=p->read(fd,data+*bytes,room,&done);
        if(r || done>room){atomic_store(&s->native_error,r);result=r?FX_IO:FX_INTEGRITY;break;}
        *bytes+=done;
        if(!done)break;
    }
    uint32_t closed=p->close(fd);if(closed)atomic_store(&s->native_error,closed);
    fx_file_observe_close(s->observer,&claim,closed);
    return closed?FX_CLOSE:result;
}
static FxResult create_record(void *context,uint32_t id,const uint8_t *data,uint32_t bytes) {
    FxStartupFiles *s=context;
    if(atomic_load(s->shutdown))return FX_STATE;
    if(id>FX_STARTUP_CFG1 || !data || bytes!=FX_STARTUP_RECORD)return FX_ARGUMENT;
    const FxStartupFileProfile *p=s->profile;
    int32_t fd=p->open(p->paths[id],0x101u,0u);
    FxCapacityClaim claim=fx_file_observe_open(s->observer,fd);
    atomic_store(&s->native_error,fd<0?(uint32_t)fd:0u);
    if(fd<0)return FX_IO;
    uint32_t done=0,r=p->write(fd,data,bytes,&done,0);
    uint32_t closed=p->close(fd);atomic_store(&s->native_error,closed?closed:r);
    fx_file_observe_close(s->observer,&claim,closed);
    return closed?FX_CLOSE:r || done!=bytes?FX_IO:FX_OK;
}
static FxResult remove_record(void *context,uint32_t id) {
    FxStartupFiles *s=context;
    if(id>FX_STARTUP_CFG1)return FX_ARGUMENT;
    if(atomic_load(s->shutdown) && (id!=FX_STARTUP_FLAG || !atomic_load(s->cleanup_window)))return FX_TIMEOUT;
    uint32_t r=s->profile->remove(s->profile->paths[id]);atomic_store(&s->native_error,r);
    return r?FX_IO:FX_OK;
}
FxResult fx_startup_files(FxStartupIO *io,FxStartupFiles *s) {
    if(!io || !s || !s->profile || !s->shutdown || !s->cleanup_window)return FX_ARGUMENT;
    const FxStartupFileProfile *p=s->profile;
    if(!p->open || !p->read || !p->write || !p->close || !p->remove)return FX_ARGUMENT;
    for(uint32_t i=0;i<3u;++i)if(!p->paths[i])return FX_ARGUMENT;
    *io=(FxStartupIO){s,read_record,create_record,remove_record};return FX_OK;
}
