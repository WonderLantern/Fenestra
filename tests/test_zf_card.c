#include <assert.h>
#include <string.h>
#include "../src/platform/zf_c301/card.h"
static unsigned opens,reads,closes;
static int32_t nextfd=7;
static unsigned alias_attempt;
static unsigned reject_long;
static int32_t long_error=(int32_t)UINT32_C(0x80000010);
static uint32_t read_result,close_result,oversize;
static int32_t open_native(const char *p,uint32_t flags,uint32_t mode) {
    assert(flags==0 && mode==0);++opens;
    if(!strcmp(p,"A:\\FENESTRA\\demo-zf.wlm"))return reject_long ? long_error : nextfd;
    assert(!strcmp(p,"A:\\FENESTRA\\DEMO-Z~1.WLM"));++alias_attempt;return nextfd;
}
static uint32_t read_native(int32_t fd,void *p,uint32_t n,uint32_t *done) {
    assert(fd==7 && p && n);++reads;*done=oversize?n+1u:n-1u;return read_result;
}
static uint32_t close_native(int32_t fd) {assert(fd==7);++closes;return close_result;}
static const FxZfCardNative native={open_native,read_native,close_native};
int main(void) {
    FxTextFiles f={0};FxZfCardScope s={0};unsigned char b[16];uint32_t done=99;
    assert(fx_zf_card_files(0,&s,&native)==FX_ARGUMENT);
    assert(fx_zf_card_files(&f,&s,0)==FX_ARGUMENT);
    assert(fx_zf_card_files(&f,&s,&native)==FX_OK && !f.write);
    const char *bad[]={0,"","B:\\FENESTRA\\demo-zf.wlm","A:\\FENESTRA\\../demo-zf.wlm", "A:\\FENESTRA\\Demo-zf.wlm",
        "A:\\FENESTRA\\demo.bin","A:\\FENESTRA\\demo.wlm/","A:\\FENESTRA\\.wlm",
        "A:\\FENESTRA\\abcdefghijklmnopqrstuvwxyz12.wlm"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(f.open(f.context,bad[i],FX_TEXT_READ)<0);
    assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_CREATE)<0 && opens==0);
    nextfd=(int32_t)UINT32_C(0x80000010);assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_READ)<0 && s.native_error==(uint32_t)nextfd);
    nextfd=INT32_C(0x7fff0000);assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_READ)<0 && s.fd==-1);
    nextfd=7;assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_READ)==7);
    assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_READ)<0 && opens==3);
    assert(fx_zf_card_files(&f,&s,&native)==FX_BUSY);
    assert(f.read(f.context,8,b,sizeof(b),&done)!=0 && done==0 && reads==0);
    assert(f.read(f.context,7,b,65537,&done)!=0 && reads==0);
    assert(f.read(f.context,7,b,sizeof(b),0)!=0 && reads==0);
    assert(f.read(f.context,7,b,sizeof(b),&done)==0 && done==15 && reads==1);
    read_result=UINT32_C(0x80002000);
    assert(f.read(f.context,7,b,sizeof(b),&done)==read_result && s.native_error==read_result);
    assert(f.close(f.context,8)!=0 && closes==0);
    assert(f.close(f.context,7)==0 && s.fd==-1 && closes==1);
    assert(fx_zf_card_files(&f,&s,&native)==FX_OK);
    assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_READ)==7);
    close_result=UINT32_C(0x80002001);
    assert(f.close(f.context,7)==close_result && s.uncertain && closes==2);
    assert(f.close(f.context,7)!=0 && closes==2);
    FxZfCardScope alias_scope={0};assert(fx_zf_card_files(&f,&alias_scope,&native)==FX_OK);
    f.context=&alias_scope;
    alias_attempt=0;
    reject_long=1;nextfd=7;assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_READ)<0 && alias_attempt==0);
    FxZfCardScope io_scope={0};assert(fx_zf_card_files(&f,&io_scope,&native)==FX_OK);
    f.context=&io_scope;
    long_error=(int32_t)UINT32_C(0x80002000);
    assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_READ)<0 && alias_attempt==0);
    assert(fx_zf_card_files(&f,&s,&native)==FX_BUSY);
    /* A separate scope tests invalid byte count without reusing uncertain fd. */
    FxZfCardScope other={0};assert(fx_zf_card_files(&f,&other,&native)==FX_OK);
    f.context=&other;reject_long=0;
    assert(f.open(f.context,"A:\\FENESTRA\\demo-zf.wlm",FX_TEXT_READ)==7);
    oversize=1;read_result=0;
    assert(f.read(f.context,7,b,sizeof(b),&done)!=0 && other.uncertain);
    assert(f.close(f.context,7)!=0 && closes==2);
    return 0;
}
