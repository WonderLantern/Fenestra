#include "fenestra_file_card.h"
#include "fenestra_memory.h"

static int same_path(const char *a,const char *b) {
    if(!a || !b) return 0;
    for(uint32_t i=0u;i<256u;++i) {
        if(a[i]!=b[i]) return 0;
        if(!a[i]) return 1;
    }
    return 0;
}
static int open_snapshot(void *context,const char *path,uint64_t *bytes) {
    FxFileCard *c=context;
    if(!c || !bytes || !c->valid || c->opened || !same_path(path,c->module_path)) return 1;
    c->opened=1u;c->position=0u;*bytes=c->bytes;return 0;
}
static int read_snapshot(void *context,uint8_t *out,uint32_t requested,uint32_t *done) {
    FxFileCard *c=context;
    if(!c || !out || !done || !c->valid || !c->opened || c->position>c->bytes) return 1;
    uint32_t n=c->bytes-c->position;
    if(n>requested) n=requested;
    /* Exact in-place Loader staging needs no second full-package copy. */
    if(out!=c->buffer+c->position)memcpy(out,c->buffer+c->position,n);
    c->position+=n;*done=n;return 0;
}
static int close_snapshot(void *context) {
    FxFileCard *c=context;
    if(!c || !c->opened) return 1;
    c->opened=0u;return 0;
}
void fx_file_card_io(FxFileCard *c,FxCardIO *io) {
    if(io) *io=(FxCardIO){c,open_snapshot,read_snapshot,close_snapshot};
}
FxResult fx_file_card_load(FxFileCard *c,const FxTextFiles *files,const char *native_path,
                          const char *module_path,uint8_t *buffer,uint32_t capacity,uint32_t expected) {
    return fx_file_card_load_observed(c,files,native_path,module_path,buffer,capacity,expected,(FxFileObserver){0});
}
FxResult fx_file_card_load_observed(FxFileCard *c,const FxTextFiles *files,const char *native_path,
                          const char *module_path,uint8_t *buffer,uint32_t capacity,uint32_t expected,FxFileObserver observer) {
    if(!c) return FX_ARGUMENT;
    if(c->opened) return FX_BUSY;
    memset(c,0,sizeof(*c));c->result=FX_ARGUMENT;
    if(!files || !files->open || !files->read || !files->close || !native_path || !module_path ||
       !buffer || !expected || expected>FX_CARD_LIMIT || capacity<=expected) return c->result;
    c->buffer=buffer;c->module_path=module_path;
    memset(buffer,0,expected+1u);
    int32_t fd=files->open(files->context,native_path,FX_TEXT_READ);
    FxCapacityClaim claim=fx_file_observe_open(observer,fd);
    if(fd<0) { c->native_error=(uint32_t)fd;c->result=FX_IO;return c->result; }
    uint32_t total=0u;FxResult result=FX_OK;
    /* The extra byte proves EOF instead of trusting the expected package size.
     * Each short transfer consumes a bounded call; native callbacks themselves
     * still require platform supervision for a hung privileged driver. */
    while(total<=expected) {
        if(c->read_calls>=FX_READ_CALL_LIMIT) { result=FX_IO;break; }
        uint32_t requested=expected+1u-total,done=0u;
        if(requested>65536u) requested=65536u;
        ++c->read_calls;
        uint32_t error=files->read(files->context,fd,buffer+total,requested,&done);
        if(error) { c->native_error=error;result=FX_IO;break; }
        if(done>requested) { result=FX_BOUNDS;break; }
        if(!done) { if(total!=expected) result=FX_BOUNDS;break; }
        total+=done;
        if(total>expected) { result=FX_BOUNDS;break; }
    }
    c->close_error=files->close(files->context,fd);
    fx_file_observe_close(observer,&claim,c->close_error);
    if(c->close_error && result==FX_OK) result=FX_CLOSE;
    c->result=result;
    if(result==FX_OK) { c->bytes=total;c->valid=1u; }
    else memset(buffer,0,expected+1u);
    return result;
}
