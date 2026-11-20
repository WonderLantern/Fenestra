#include <assert.h>
#include <string.h>
#include <stdint.h>
#undef memset
#undef memcpy
#undef memmove
static uint32_t native_open(const char *,int32_t *);
static uint32_t native_next(int32_t,void *);
static uint32_t native_close(int32_t);
#define FX_Z63_NATIVE_CALL(a,t) ((t)((a)==0x402e695cu?(uintptr_t)native_open:(a)==0x402e6a0cu?(uintptr_t)native_next:(uintptr_t)native_close))
#include "../src/platform/z63_c201/discovery.c"
static uint8_t record[0x220];static uint32_t next_result,close_result;
static uint32_t native_open(const char *path,int32_t *out){assert(!strcmp(path,"A:\\FENESTRA"));*out=7;return 0;}
static uint32_t native_next(int32_t handle,void *out){assert(handle==7);memcpy(out,record,sizeof(record));return next_result;}
static uint32_t native_close(int32_t handle){assert(handle==7);return close_result;}
FxResult fx_z63_card_files(FxTextFiles *f,FxZ63CardScope *s){memset(f,0,sizeof(*f));s->fd=-1;return FX_OK;}
int fx_service_name(const char *s,uint32_t n,int dot){(void)s;(void)n;(void)dot;return 0;}
int main(void) {
 FxZ63Directory scope={0};FxModuleDirectory io;FxModuleDirectoryEntry entry;
 assert(fx_z63_directory_init(&scope,&io)==FX_OK);
 assert(io.open(io.context)==FX_OK);
 assert(fx_z63_directory_init(&scope,&io)==FX_BUSY);
 memcpy(record,"DEMO.WLM",9);record[0x20e]=0x20;record[0x218]=0x34;record[0x219]=0x12;record[0x21c]=1;
 assert(io.next(io.context,&entry)==FX_OK && !strcmp(entry.name,"demo.wlm") && entry.attributes==0x20 && entry.bytes==UINT64_C(0x100001234));
 memset(record,0,sizeof(record));const char *long_name="MixedCase.wlm";
 for(uint32_t i=0;long_name[i];++i)record[14+2*i]=(uint8_t)long_name[i];
 assert(io.next(io.context,&entry)==FX_OK && !strcmp(entry.name,"mixedcase.wlm"));
 record[15]=1;assert(io.next(io.context,&entry)==FX_OK && !entry.name[0]);
 next_result=0x80000030u;assert(io.next(io.context,&entry)==FX_EMPTY);
 close_result=9;assert(io.close(io.context)==FX_CLOSE && scope.uncertain);
 assert(fx_z63_directory_init(&scope,&io)==FX_BUSY);
 assert(io.open(io.context)==FX_BUSY);
 return 0;
}
