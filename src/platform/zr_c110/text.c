/* Independently inspected C1.10 filesystem wrappers, native RTOS view only.
 * No raw sectors, host filesystem, QEMU register or physical camera command. */
#include "fenestra_text.h"
#ifdef FX_C110_FILE_POLICY_TEST
extern uintptr_t fx_file_policy_native(uintptr_t);
#define FN(a,t) ((t)fx_file_policy_native((uintptr_t)(a)))
#else
#define FN(a,t) ((t)(uintptr_t)(a))
#endif
#define DENIED UINT32_C(0x80000001)
int fx_c110_card_not_ready(uint32_t native_error) {
    /* 40913680: no registered drive; 40913620: uninitialized/not mounted,
     * or mount state 2. These precede opening a file descriptor. */
    return native_error==UINT32_C(0x80001003) ||
           native_error==UINT32_C(0x80001005) ||
           native_error==UINT32_C(0x88000105);
}
static int equal(const char *a,const char *b) {
    if(!a) return 0;
    for(uint32_t i=0u;i<256u;++i) {
        if(a[i]!=b[i]) return 0;
        if(!a[i]) return 1;
    }
    return 0;
}
static int allowed(const char *path,uint32_t mode) {
    if(mode==FX_TEXT_CREATE) return equal(path,FX_TEXT_PATH);
    if(mode!=FX_TEXT_READ) return 0;
    if(equal(path,FX_TEXT_PATH) || equal(path,"A:\\FENESTRA\\demo.wlm")) return 1;
#ifdef FX_C110_FAULT_TEST
    if(equal(path,"A:\\FENESTRA\\faulty.wlm")) return 1;
#endif
    return 0;
}
static int32_t open_file(void *context,const char *path,uint32_t mode) {
    FxC110FileScope *s=context;
    if(!s || s->uncertain || s->fd>=0 || !allowed(path,mode)) return -1;
    /* 0x101 = native write + exclusive create. 40904fac..40904ffc rejects
     * existing entries with 0x80000002. No truncate/append flags are set.
     * ASCII drive-qualified backslash paths are parsed by 409136fc. */
    int32_t fd=FN(UINT64_C(0x402e883c),int32_t (*)(const char *,uint32_t,uint32_t))
        (path,mode==FX_TEXT_CREATE?0x101u:0u,0u);
    if(fd>=0) { s->fd=fd;s->mode=mode; }
    return fd;
}
static uint32_t write_file(void *context,int32_t fd,const void *data,uint32_t size,uint32_t *done) {
    FxC110FileScope *s=context;
    if(done) *done=0u;
    if(!s || s->uncertain || fd<0 || fd!=s->fd || s->mode!=FX_TEXT_CREATE || !data || !done ||
       !size || size>sizeof(FX_TEXT_CONTENT)-1u) return DENIED;
    return FN(UINT64_C(0x402e8ad0),uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *))
        (fd,data,size,done,0);
}
static uint32_t read_file(void *context,int32_t fd,void *data,uint32_t size,uint32_t *done) {
    FxC110FileScope *s=context;
    if(done) *done=0u;
    if(!s || s->uncertain || fd<0 || fd!=s->fd || s->mode!=FX_TEXT_READ || !data || !done ||
       !size || size>65536u) return DENIED;
    return FN(UINT64_C(0x402e8a0c),uint32_t (*)(int32_t,void *,uint32_t,uint32_t *))(fd,data,size,done);
}
static uint32_t close_file(void *context,int32_t fd) {
    FxC110FileScope *s=context;
    if(!s || s->uncertain || fd<0 || fd!=s->fd) return DENIED;
    uint32_t result=FN(UINT64_C(0x402e889c),uint32_t (*)(int32_t))(fd);
    if(result)s->uncertain=1u;
    else {s->fd=-1;s->mode=0u;}
    return result;
}
void fx_c110_text_files(FxTextFiles *files,FxC110FileScope *scope) {
    if(!files || !scope) return;
    *scope=(FxC110FileScope){.fd=-1};
    *files=(FxTextFiles){scope,open_file,write_file,read_file,close_file};
}
