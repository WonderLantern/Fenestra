/* Read-only module snapshots through normal serialized firmware wrappers.
 * No discovery, mount, sector/controller operation, retries or module fd ABI. */
#include "card.h"
#include "fenestra_memory.h"
#define MAGIC UINT32_C(0x465a4643)
#define DENIED UINT32_C(0x80000001)
static int module_path(const char *p) {
    static const char prefix[]="A:\\FENESTRA\\";
    if(!p)return 0;
    for(uint32_t i=0;i<sizeof(prefix)-1u;++i)if(p[i]!=prefix[i])return 0;
    p+=sizeof(prefix)-1u;
    uint32_t n=0;
    while(n<32u && p[n])++n;
    if(n<5u || n>=32u || p[n-4u]!='.' || p[n-3u]!='w' || p[n-2u]!='l' || p[n-1u]!='m')return 0;
    for(uint32_t i=0;i<n-4u;++i)
        if(!((p[i]>='a' && p[i]<='z') || (p[i]>='0' && p[i]<='9') || p[i]=='_' || p[i]=='-'))return 0;
    return 1;
}
static int32_t open_file(void *context,const char *path,uint32_t mode) {
    FxZfCardScope *s=context;
    if(!s || s->magic!=MAGIC || s->uncertain || s->fd>=0 || mode!=FX_TEXT_READ || !module_path(path))return -1;
    int32_t fd=s->native->open(path,0u,0u);
    /* Own-Zf caller explicitly distinguishes this reserved handle from fd. */
    if(fd<0 || fd==INT32_C(0x7fff0000)){s->native_error=(uint32_t)fd;return -1;}
    s->fd=fd;
    return fd;
}
static uint32_t read_file(void *context,int32_t fd,void *data,uint32_t bytes,uint32_t *done) {
    FxZfCardScope *s=context;
    if(done)*done=0;
    if(!s || s->magic!=MAGIC || s->uncertain || fd<0 || fd!=s->fd || !data || !done || !bytes || bytes>65536u)return DENIED;
    uint32_t result=s->native->read(fd,data,bytes,done);
    if(result)s->native_error=result;
    if(*done>bytes){s->uncertain=1u;return DENIED;}
    return result;
}
static uint32_t close_file(void *context,int32_t fd) {
    FxZfCardScope *s=context;
    if(!s || s->magic!=MAGIC || s->uncertain || fd<0 || fd!=s->fd)return DENIED;
    uint32_t result=s->native->close(fd);
    if(result){s->native_error=result;s->uncertain=1u;}
    else s->fd=-1;
    return result;
}
FxResult fx_zf_card_files(FxTextFiles *files,FxZfCardScope *s,const FxZfCardNative *native) {
    if(!files || !s || !native || !native->open || !native->read || !native->close)return FX_ARGUMENT;
    if(s->magic==MAGIC && (s->fd>=0 || s->uncertain))return FX_BUSY;
    *s=(FxZfCardScope){MAGIC,0u,0u,-1,native};
    *files=(FxTextFiles){s,open_file,0,read_file,close_file};
    return FX_OK;
}
