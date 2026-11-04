/* Fault-injected normal-file backend: an uncertain close must prevent every
 * subsequent journal IO, including a guard clear. */
#include "fenestra_startup_files.h"
#include <assert.h>
#include <string.h>
static uint32_t opens,closes,removes,writes,position,fail_close;
static int32_t opened(const char *p,uint32_t mode,uint32_t unused) {
    (void)p;(void)mode;(void)unused;++opens;position=0u;return 7;
}
static uint32_t read_bytes(int32_t fd,void *p,uint32_t n,uint32_t *done) {
    assert(fd==7);uint32_t left=64u-position;if(n>7u)n=7u;if(n>left)n=left;
    memset(p,0x5a,n);*done=n;position+=n;return 0u;
}
static uint32_t written(int32_t fd,const void *p,uint32_t n,uint32_t *done,void *v) {
    (void)p;(void)v;assert(fd==7);++writes;*done=n;return 0u;
}
static uint32_t closed(int32_t fd){assert(fd==7);++closes;return fail_close?123u:0u;}
static uint32_t removed(const char *p){(void)p;++removes;return 0u;}
int main(void) {
    _Atomic uint32_t stop=0u,window=0u;
    FxStartupFileProfile profile={opened,read_bytes,written,closed,removed,{"flag","cfg0","cfg1"},0x80000010u};
    FxStartupFiles files={.profile=&profile,.shutdown=&stop,.cleanup_window=&window};FxStartupIO io;
    assert(fx_startup_files(&io,&files)==FX_OK);
    uint8_t data[65];uint32_t bytes=0;
    assert(io.read(io.context,0u,data,65u,&bytes)==FX_OK && bytes==64u && opens==1u && closes==1u);
    for(uint32_t i=0;i<64u;++i)assert(data[i]==0x5au);
    assert(io.create(io.context,0u,data,64u)==FX_OK && writes==1u);
    fail_close=1u;
    assert(io.read(io.context,0u,data,65u,&bytes)==FX_CLOSE);
    assert(atomic_load(&files.uncertain)==1u && atomic_load(&files.native_error)==123u);
    uint32_t old=opens;
    assert(io.read(io.context,0u,data,65u,&bytes)==FX_CLOSE && !bytes);
    assert(io.create(io.context,0u,data,64u)==FX_CLOSE);
    assert(io.remove(io.context,0u)==FX_CLOSE && !removes && opens==old);
    return 0;
}
