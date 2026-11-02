#include <assert.h>
#include <stdint.h>
static int32_t fake_open(const char *,uint32_t,uint32_t);
static uint32_t fake_read(int32_t,void *,uint32_t,uint32_t *);
static uint32_t fake_close(int32_t);
#define FX_Z63_NATIVE_CALL(a,t) ((t)((a)==0x402e3674u? (uintptr_t)fake_open: (a)==0x402e3774u? (uintptr_t)fake_read:(uintptr_t)fake_close))
#include "../src/platform/z63_c201/card.c"
static uint32_t requested,result,count,close_result,calls,omit_count;
static int32_t fake_open(const char *p,uint32_t a,uint32_t b){assert(p && !a && !b);return 3;}
static uint32_t fake_read(int32_t fd,void *p,uint32_t n,uint32_t *done){assert(fd==3 && p);++calls;requested=n;if(!omit_count)*done=count;return result;}
static uint32_t fake_close(int32_t fd){assert(fd==3);return close_result;}
int main(void){
 FxZ63CardScope s={0};FxTextFiles f;char data[1024];uint32_t done;
 assert(fx_z63_card_files(&f,&s)==FX_OK);
 assert(f.open(f.context,"A:\\FENESTRA\\../x.wlm",FX_TEXT_READ)<0);
 assert(f.open(f.context,"A:\\FENESTRA\\probe-z63.wlm",FX_TEXT_READ)<0);
 assert(f.open(f.context,"A:\\FENESTRA\\demo-z63.wlm",FX_TEXT_READ)==3);
 assert(fx_z63_card_files(&f,&s)==FX_BUSY);
 count=17;assert(!f.read(f.context,3,data,sizeof(data),&done) && done==17 && requested==511);
 count=0;assert(!f.read(f.context,3,data,10,&done) && !done && requested==10);
 result=0x80000901u;assert(f.read(f.context,3,data,10,&done)==result && s.native_error==result);
 result=0;count=512;assert(f.read(f.context,3,data,sizeof(data),&done)==DENIED && s.uncertain);
 uint32_t before=calls;assert(f.read(f.context,3,data,10,&done)==DENIED && calls==before);
 assert(fx_z63_card_files(&f,&s)==FX_BUSY);
 s=(FxZ63CardScope){0};assert(fx_z63_card_files(&f,&s)==FX_OK);
 assert(f.open(f.context,"A:\\FENESTRA\\demo.wlm",FX_TEXT_READ)==3);
 close_result=9;assert(f.close(f.context,3)==9 && s.uncertain && s.fd==3);
 s=(FxZ63CardScope){0};assert(fx_z63_card_files(&f,&s)==FX_OK);
 assert(f.open(f.context,"A:\\FENESTRA\\demo.wlm",FX_TEXT_READ)==3);
 close_result=0;assert(!f.close(f.context,3) && s.fd==-1);
 assert(f.open(f.context,"A:\\FENESTRA\\demo.wlm",FX_TEXT_READ)==3);
 omit_count=1;assert(f.read(f.context,3,data,10,&done)==DENIED && s.uncertain);
 return 0;
}
