#include "module_stage.h"
#include "fenestra_memory.h"
/* Own complete Zf C3.01 EG payload SHA256, excluding update CRC. */
static const uint8_t target[32]={
    0x68,0x91,0xed,0xd2,0x2d,0x9a,0x40,0x04,0x2e,0x47,0x18,0x48,0x0d,0x29,0x08,0x26,
    0x82,0xbb,0xd1,0x2a,0xea,0x17,0x2c,0x51,0xf4,0xf1,0x64,0xb9,0x7f,0x74,0xfe,0x08};
static int overlap(const void *a,uint32_t an,const void *b,uint32_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>UINTPTR_MAX-an || y>UINTPTR_MAX-bn || (x<y+bn && y<x+an);
}
static int paths(const char *native,const char *logical,char copy[48]) {
    const char a[]="A:\\FENESTRA\\",b[]="FENESTRA/";
    for(uint32_t i=0;i<sizeof(a)-1u;++i)if(native[i]!=a[i])return 0;
    for(uint32_t i=0;i<sizeof(b)-1u;++i)if(logical[i]!=b[i])return 0;
    uint32_t i=0;
    for(;i<32u;++i) {
        char c=logical[sizeof(b)-1u+i];
        if(native[sizeof(a)-1u+i]!=c)return 0;
        copy[sizeof(b)-1u+i]=c;if(!c)break;
    }
    if(i<5u || i>=32u)return 0;
    memcpy(copy,b,sizeof(b)-1u);return 1;
}
FxResult fx_zf_module_stage(FxZfModuleStage *s,const FxZfCardNative *native_ops,
    const char *native_path,const char *logical_path,uint32_t bytes,const uint8_t digest[32],uint64_t capabilities,
    uint8_t *file,uint32_t file_capacity,uint8_t *package,uint32_t package_capacity,
    uint8_t *image,uint32_t image_capacity,uint64_t now) {
    if(!s || !native_ops || !native_path || !logical_path || !digest || !bytes || bytes>FX_CARD_LIMIT ||
       !file || file_capacity<=bytes || file_capacity>FX_CARD_LIMIT+1u ||
       !package || package_capacity<bytes || package_capacity>FX_CARD_LIMIT ||
       !image || !image_capacity || image_capacity>FX_MODULE_MEMORY_LIMIT ||
       ((uintptr_t)image&4095u) || now>UINT64_MAX-10000u ||
       (file!=package && overlap(file,file_capacity,package,package_capacity)) ||
       overlap(file,file_capacity,image,image_capacity) || overlap(package,package_capacity,image,image_capacity) ||
       overlap(s,(uint32_t)sizeof(*s),file,file_capacity) || overlap(s,(uint32_t)sizeof(*s),package,package_capacity) ||
       overlap(s,(uint32_t)sizeof(*s),image,image_capacity))return FX_ARGUMENT;
    if(s->attempted)return FX_BUSY;
    char logical[48]={0},native[64]="A:\\FENESTRA\\";uint8_t pin[32];
    if(!paths(native_path,logical_path,logical))return FX_ARGUMENT;
    s->attempted=1u;
    memcpy(native+12u,logical+9u,32u);memcpy(pin,digest,32u);
    FxTextFiles files;FxCardIO io;
    memcpy(s->module_path,logical,sizeof(logical));memcpy(s->digest,pin,sizeof(pin));
    FxResult r=fx_zf_card_files(&files,&s->native,native_ops);
    if(r==FX_OK)r=fx_file_card_load_observed(&s->file,&files,native,s->module_path,file,file_capacity,bytes,s->file_observer);
    if(r==FX_OK) {
        fx_file_card_io(&s->file,&io);
        fx_sha256(s->file.buffer,s->file.bytes,s->snapshot_digest);
        fx_loader_init(&s->loader,capabilities,(uint64_t)package_capacity+image_capacity);
        r=fx_loader_prepare(&s->loader,&io,s->module_path,bytes,s->digest,target,package,package_capacity,
                            image,image_capacity,now,10000u);
        s->card_receipt=s->loader.card;
        s->runtime_phase=s->loader.runtime.phase;
    }
    s->result=r;return r;
}
