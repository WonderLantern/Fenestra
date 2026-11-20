#include "fenestra_services.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Card listing as seen on a FAT/exFAT card written from macOS: AppleDouble
 * companions sit next to real packages and share their .wlm suffix. */
static const char *const names[]={
    "._slotcard.wlm","._slotcard.wlm.disabled",".wlm.wlm","bad..name.wlm",
    "identity_too_long.wlm","Upper.wlm","slotcard.wlm.disabled","notes.txt",
};
static uint32_t cursor,opened,closed,reads;
static FxResult dir_open(void *c){(void)c;cursor=0;++opened;return FX_OK;}
static FxResult dir_next(void *c,FxModuleDirectoryEntry *e) {
    (void)c;if(cursor==sizeof(names)/sizeof(names[0]))return FX_EMPTY;
    memset(e,0,sizeof(*e));strncpy(e->name,names[cursor++],sizeof(e->name)-1u);e->bytes=4096u;
    return FX_OK;
}
static FxResult dir_close(void *c){(void)c;++closed;return FX_OK;}
static FxResult dir_read(void *c,const char *n,uint8_t *b,uint32_t cap,uint32_t *got) {
    (void)c;(void)n;(void)b;(void)cap;(void)got;++reads;return FX_IO;
}
static void *allocate(void *c,uint32_t bytes){(void)c;return calloc(1u,bytes);}
static void release(void *c,void *p){(void)c;free(p);}
int main(void) {
    static uint8_t scratch[4096];uint8_t firmware[32]={0};
    FxModuleDirectory io={0,dir_open,dir_next,dir_close,dir_read};
    FxModuleInventory out={0};
    FxResult r=fx_modules_discover(&io,firmware,0u,scratch,sizeof(scratch),&out,(FxIndexMemory){0,allocate,release});
    printf("discover=%d count=%u reads=%u\n",(int)r,out.count,reads);
    assert(r==FX_OK);
    assert(out.count==0u && !reads); /* only a disabled valid package remains */
    assert(opened==1u && closed==1u);
    fx_modules_destroy(&out);
    puts("discovery fixture passed");
    return 0;
}
