#include "fenestra_text.h"
#include "fenestra_memory.h"

static FxResult failure(FxTextReport *r,uint32_t native,FxResult result) {
    if(!r->native_error) r->native_error=native;
    r->result=(uint32_t)result;r->state=FX_TEXT_FAILED;
    return result;
}
FxResult fx_text_write(const FxTextFiles *f,FxTextReport *r) {
    return fx_text_write_observed(f,r,(FxFileObserver){0});
}
FxResult fx_text_write_observed(const FxTextFiles *f,FxTextReport *r,FxFileObserver observer) {
    static const char content[]=FX_TEXT_CONTENT;
    const uint32_t length=(uint32_t)sizeof(content)-1u;
    uint8_t check[sizeof(content)];
    if(!r) return FX_ARGUMENT;
    uint32_t attempts=r->attempts;
    memset(r,0,sizeof(*r));
    r->magic=0x46585431u;r->version=1u;r->attempts=attempts+1u;
    if(!f || !f->open || !f->write || !f->read || !f->close)
        return failure(r,0u,FX_ARGUMENT);
    r->state=FX_TEXT_WRITING;r->step=1u;
    int32_t fd=f->open(f->context,FX_TEXT_PATH,FX_TEXT_CREATE);
    FxCapacityClaim claim=fx_file_observe_open(observer,fd);
    if(fd<0) return failure(r,(uint32_t)fd,FX_IO);
    uint32_t error=0u,total=0u;
    r->step=2u;
    while(total<length) {
        uint32_t done=0u;
        error=f->write(f->context,fd,content+total,length-total,&done);
        if(done>length-total) { error=0u;break; }
        total+=done;r->bytes=total;
        if(error || !done) break;
    }
    /* C1.10 close commits file metadata and flushes its dirty buffers. Always
     * close once, including after a short/error write; preserve first error. */
    r->step=3u;
    uint32_t closed=f->close(f->context,fd);
    fx_file_observe_close(observer,&claim,closed);
    if(error || closed || total!=length) return failure(r,error?error:closed,FX_IO);
    r->step=4u;
    fd=f->open(f->context,FX_TEXT_PATH,FX_TEXT_READ);
    claim=fx_file_observe_open(observer,fd);
    if(fd<0) return failure(r,(uint32_t)fd,FX_IO);
    r->step=5u;total=0u;
    while(total<sizeof(check)) {
        uint32_t done=0u;
        error=f->read(f->context,fd,check+total,(uint32_t)sizeof(check)-total,&done);
        if(done>sizeof(check)-total) { total=0u;error=0u;break; }
        total+=done;
        if(error || !done) break;
    }
    r->step=6u;closed=f->close(f->context,fd);
    fx_file_observe_close(observer,&claim,closed);
    if(error || closed) return failure(r,error?error:closed,FX_IO);
    if(total!=length || memcmp(check,content,length)!=0) return failure(r,0u,FX_INTEGRITY);
    r->step=7u;r->result=FX_OK;r->state=FX_TEXT_SAVED;
    return FX_OK;
}
