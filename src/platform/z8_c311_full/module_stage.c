/* Z8 port derived from upstream src/platform/z9_c532/module_stage.c.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#include "module_stage.h"
#include "fenestra_memory.h"
#include "fenestra_services.h"
/* Independently pinned C3.11 MAIN identity; intentionally not the ZR identity. */
static const uint8_t target[32]=FX_Z8_APPLICATION_IDENTITY;
static int overlap(const void *a,uint32_t an,const void *b,uint32_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>UINTPTR_MAX-an || y>UINTPTR_MAX-bn || (x<y+bn && y<x+an);
}
static int paths(const char *native,const char *module,char copy[48]) {
    const char a[]="A:\\FENESTRA\\",b[]="FENESTRA/";
    char drive=fx_z8_card_drive();if(!drive || native[0]!=drive)return 0;
    for(uint32_t i=1u;i<sizeof(a)-1u;++i)if(native[i]!=a[i])return 0;
    for(uint32_t i=0;i<sizeof(b)-1u;++i)if(module[i]!=b[i])return 0;
    uint32_t i=0;
    for(;i<32u;++i) {
        char c=module[sizeof(b)-1u+i];
        if(native[sizeof(a)-1u+i]!=c)return 0;
        copy[sizeof(b)-1u+i]=c;if(!c)break;
    }
    if(i<5u || i>=32u)return 0;
    memcpy(copy,b,sizeof(b)-1u);return 1;
}
FxResult fx_z8_module_stage(FxZ8ModuleStage *s,const char *native_path,const char *module_path,
                          uint32_t bytes,const uint8_t digest[32],uint64_t capabilities,
                          uint8_t *file_buffer,uint32_t file_capacity,
                          uint8_t *package,uint32_t package_capacity,
                          uint8_t *image,uint32_t image_capacity,uint64_t now) {
    if(!s || !native_path || !module_path || !digest || !bytes || bytes>FX_CARD_LIMIT ||
       !file_buffer || file_capacity<=bytes || file_capacity>FX_CARD_LIMIT+1u ||
       !package || package_capacity<bytes || package_capacity>FX_CARD_LIMIT ||
       !image || !image_capacity || image_capacity>FX_MODULE_MEMORY_LIMIT ||
       ((uintptr_t)image&4095u) || now>UINT64_MAX-10000u ||
       (file_buffer!=package && overlap(file_buffer,file_capacity,package,package_capacity)) ||
       overlap(file_buffer,file_capacity,image,image_capacity) ||
       overlap(package,package_capacity,image,image_capacity) ||
       overlap(s,(uint32_t)sizeof(*s),file_buffer,file_capacity) ||
       overlap(s,(uint32_t)sizeof(*s),package,package_capacity) ||
       overlap(s,(uint32_t)sizeof(*s),image,image_capacity))return FX_ARGUMENT;
    if(s->attempted)return FX_BUSY;
    /* Inputs may reside in discovery RAM beside this scope; snapshot them before
     * any native read or Loader initialization can change scratch storage. */
    char logical[48]={0},native[64]="A:\\FENESTRA\\";uint8_t pin[32];
    if(!paths(native_path,module_path,logical))return FX_ARGUMENT;
    native[0]=native_path[0];
    memcpy(native+12u,logical+9u,32u);
    memcpy(pin,digest,sizeof(pin));
    s->attempted=1u;FxTextFiles files;FxCardIO io;
    memcpy(s->module_path,logical,sizeof(logical));memcpy(s->digest,pin,sizeof(pin));
    FxResult r=fx_z8_card_files(&files,&s->native);
    if(r==FX_OK)r=fx_file_card_load_observed(&s->file,&files,native,s->module_path,file_buffer,file_capacity,bytes,s->file_observer);
    if(r==FX_OK) {
        fx_file_card_io(&s->file,&io);
        uint64_t budget=(uint64_t)package_capacity+image_capacity;
        fx_loader_init(&s->loader,capabilities,budget);
        r=fx_loader_prepare(&s->loader,&io,s->module_path,bytes,s->digest,target,package,package_capacity,
                            image,image_capacity,now,10000u);
    }
    s->result=r;return r;
}
