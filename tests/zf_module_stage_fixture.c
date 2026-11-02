#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform/zf_c301/module_stage.h"
static unsigned char *source;
static uint32_t source_bytes,image_capacity,position,opens,closes,read_error,close_error;
static uint32_t word(uint32_t at) {
    assert(at<=source_bytes-4u);
    return (uint32_t)source[at]|(uint32_t)source[at+1u]<<8|
        (uint32_t)source[at+2u]<<16|(uint32_t)source[at+3u]<<24;
}
static int32_t open_native(const char *path,uint32_t flags,uint32_t mode) {
    assert(!strcmp(path,"A:\\FENESTRA\\demo-zf.wlm") && !flags && !mode);++opens;position=0;return 7;
}
static uint32_t read_native(int32_t fd,void *out,uint32_t n,uint32_t *done) {
    assert(fd==7);if(read_error){*done=0;return read_error;}
    if(n>source_bytes-position)n=source_bytes-position;
    if(n>997)n=997; /* Real snapshot loop must handle repeated short reads. */
    memcpy(out,source+position,n);position+=n;*done=n;return 0;
}
static uint32_t close_native(int32_t fd){assert(fd==7);++closes;return close_error;}
static const FxZfCardNative native={open_native,read_native,close_native};
static FxResult stage(FxZfModuleStage *s,const uint8_t *pin,uint64_t caps,
                      uint8_t *file,uint8_t *package,uint8_t *image) {
    return fx_zf_module_stage(s,&native,"A:\\FENESTRA\\demo-zf.wlm","FENESTRA/demo-zf.wlm",source_bytes,pin,caps,
        file,source_bytes+1u,package,source_bytes,image,image_capacity,10u);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>128 && size<1048576);rewind(f);
    source_bytes=(uint32_t)size;source=malloc(source_bytes);assert(source);
    assert(fread(source,1,source_bytes,f)==source_bytes);fclose(f);
    uint32_t expected_image=word(92u),expected_entry=word(96u);
    assert(expected_image && expected_image<=4u*1024u*1024u);
    image_capacity=(expected_image+4095u)&~4095u;
    uint8_t pin[32];fx_sha256(source,source_bytes,pin);
    uint8_t *file=malloc(source_bytes+1u),*package=malloc(source_bytes);
    void *image_raw=malloc((size_t)image_capacity+4095u);assert(image_raw);
    uint8_t *image=(uint8_t *)(((uintptr_t)image_raw+4095u)&~(uintptr_t)4095u);
    assert(file && package && image);FxZfModuleStage s={0};
    assert(stage(&s,pin,UINT64_C(0x3f0000),file,package,image)==FX_OK);
    assert(s.loader.module.prepared && s.loader.module.bytes==expected_image && s.loader.module.entry_offset==expected_entry);
    assert(s.loader.runtime.phase==FX_PREPARED && !s.loader.exec_arena && s.native.fd==-1 && !s.native.uncertain);
    assert(s.file.valid && s.file.read_calls>=source_bytes/997u+1u && opens==1 && closes==1);
    assert(!memcmp(s.loader.target_firmware_sha256,source+24,32));
    assert(stage(&s,pin,UINT64_MAX,file,package,image)==FX_BUSY && opens==1);
    memset(&s,0,sizeof(s));pin[0]^=1;
    assert(stage(&s,pin,UINT64_MAX,file,package,image)==FX_INTEGRITY && !s.loader.module.prepared);
    pin[0]^=1;memset(&s,0,sizeof(s));
    assert(stage(&s,pin,0,file,package,image)==FX_CAPABILITY && !s.loader.module.prepared);
    /* Correctly rehashed file for a different firmware must still be rejected. */
    source[24]^=1;fx_sha256(source,source_bytes,pin);memset(&s,0,sizeof(s));
    assert(stage(&s,pin,UINT64_MAX,file,package,image)==FX_CAPABILITY);
    source[24]^=1;fx_sha256(source,source_bytes,pin);memset(&s,0,sizeof(s));
    read_error=UINT32_C(0x80002000);
    assert(stage(&s,pin,UINT64_MAX,file,package,image)==FX_IO && !s.file.valid && !s.loader.module.prepared);
    read_error=0;close_error=UINT32_C(0x80002001);memset(&s,0,sizeof(s));
    assert(stage(&s,pin,UINT64_MAX,file,package,image)==FX_CLOSE && s.native.uncertain && !s.loader.module.prepared);
    close_error=0;memset(&s,0,sizeof(s));unsigned old=opens;
    assert(fx_zf_module_stage(&s,&native,"A:\\FENESTRA\\demo-zf.wlm","FENESTRA/other.wlm",source_bytes,pin,UINT64_MAX,
        file,source_bytes+1u,file,source_bytes,image,image_capacity,10)==FX_ARGUMENT && !s.attempted && opens==old);
    free(image_raw);free(package);free(file);free(source);puts("PASS Zf own-target stage, distinct buffers, short reads, exact EOF, close, digest/capability/target failure; no execution");return 0;
}
