#include "storage.h"
#include "fenestra_memory.h"

void fx_z8_storage_init(FxZ8Storage *s,const FxStartupFileProfile *profile) {
    memset(s,0,sizeof(*s));
    s->profile=*profile;
    memcpy(s->prefix,"A:\\FENESTRA\\",13u);
    for(uint32_t i=0;i<3u;++i) {
        uint32_t n=0u;
        while(profile->paths[i][n] && n<31u){s->paths[i][n]=profile->paths[i][n];++n;}
        s->paths[i][n]=0;s->profile.paths[i]=s->paths[i];
    }
}
char fx_z8_storage_drive(const FxZ8Storage *s) {
    uint32_t selected=s?atomic_load_explicit(&s->selected,memory_order_acquire):0u;
    return selected==1u?'A':selected==2u?'B':0;
}
FxResult fx_z8_storage_select(FxZ8Storage *s,
    uint32_t (*open_directory)(const char *,int32_t *),uint32_t (*close_directory)(int32_t)) {
    if(!s || !open_directory || !close_directory)return FX_ARGUMENT;
    if(fx_z8_storage_drive(s))return FX_OK;
    if(s->latched_error)return s->latched_error;
    ++s->attempts;
    for(uint32_t slot=0u;slot<2u;++slot) {
        char directory[]="A:\\FENESTRA";
        directory[0]=(char)('A'+slot);
        int32_t fd=-1;
        uint32_t r=open_directory(directory,&fd);
        if(r || fd<0) {
            s->native_error=r?r:(uint32_t)fd;
            /* A failing call that nevertheless publishes a descriptor has
             * uncertain ownership. Never reopen or fail over in that case. */
            if(fd>=0){s->latched_error=FX_CLOSE;return FX_CLOSE;}
            continue;
        }
        r=close_directory(fd);
        if(r){s->native_error=r;s->latched_error=FX_CLOSE;return FX_CLOSE;}
        s->prefix[0]=directory[0];
        for(uint32_t i=0;i<3u;++i)s->paths[i][0]=directory[0];
        s->native_error=0u;
        atomic_store_explicit(&s->selected,slot+1u,memory_order_release);
        return FX_OK;
    }
    return FX_IO;
}
