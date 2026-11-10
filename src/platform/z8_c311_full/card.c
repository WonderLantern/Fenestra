/* Z8 port derived from upstream src/platform/z9_c532/card.c.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
/* Z8 C3.11 normal filesystem read path. No controller/sector/Flash interface.
 * Addresses are independent Z8 bindings; the original serialized wrappers
 * perform their normal locking and short-read handling. */
#include "card.h"
#include "fenestra_z8_file_read.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#define MAGIC UINT32_C(0x465a3843)
#define DENIED UINT32_C(0x80000001)

static int module_path(const char *p) {
    static const char prefix[]="A:\\FENESTRA\\";
    char drive=fx_z8_card_drive();if(!p || !drive || p[0]!=drive)return 0;
    for(uint32_t i=1u;i<sizeof(prefix)-1u;++i)if(p[i]!=prefix[i])return 0;
    p+=sizeof(prefix)-1u;
    uint32_t n=0;
    while(n<32u && p[n])++n;
    if(n<5u || n>=32u || p[n-4u]!='.' || p[n-3u]!='w' || p[n-2u]!='l' || p[n-1u]!='m')return 0;
    for(uint32_t i=0;i<n-4u;++i)
        if(!((p[i]>='a' && p[i]<='z') || (p[i]>='0' && p[i]<='9') || p[i]=='_' || p[i]=='-'))return 0;
    return 1;
}
static int32_t open_file(void *context,const char *path,uint32_t mode) {
    FxZ8CardScope *s=context;
    if(!s || s->magic!=MAGIC || s->uncertain || s->fd>=0 || mode!=FX_TEXT_READ || !module_path(path))return -1;
    int32_t fd=FN(FX_Z8_ADDR_402dc960,int32_t (*)(const char *,uint32_t,uint32_t))(path,0u,0u);
    if(fd<0)s->native_error=(uint32_t)fd;
    else s->fd=fd;
    return fd;
}
static uint32_t read_file(void *context,int32_t fd,void *data,uint32_t bytes,uint32_t *done) {
    FxZ8CardScope *s=context;
    if(done)*done=0;
    if(!s || s->magic!=MAGIC || s->uncertain || fd<0 || fd!=s->fd || !data || !done || !bytes || bytes>65536u)return DENIED;
    uint32_t result=fx_z8_file_read(fd,data,bytes,done);
    if(result)s->native_error=result;
    if(*done>bytes){s->uncertain=1u;return DENIED;}
    return result;
}
static uint32_t close_file(void *context,int32_t fd) {
    FxZ8CardScope *s=context;
    if(!s || s->magic!=MAGIC || s->uncertain || fd<0 || fd!=s->fd)return DENIED;
    uint32_t result=FN(FX_Z8_ADDR_402dc9c0,uint32_t (*)(int32_t))(fd);
    if(result){s->native_error=result;s->uncertain=1u;}
    else s->fd=-1;
    return result;
}
FxResult fx_z8_card_files(FxTextFiles *files,FxZ8CardScope *scope) {
    if(!files || !scope)return FX_ARGUMENT;
    if(scope->magic==MAGIC && (scope->fd>=0 || scope->uncertain))return FX_BUSY;
    *scope=(FxZ8CardScope){MAGIC,0u,0u,-1};
    *files=(FxTextFiles){scope,open_file,0,read_file,close_file};
    return FX_OK;
}
