/* C2.01 serialized native directory wrappers. The directory handle is separate
 * from the FILESRV stream; uncertainty closes admission until reboot. */
#include "discovery.h"
#include "fenestra_memory.h"
#ifndef FX_Z63_NATIVE_CALL
#define FX_Z63_NATIVE_CALL(a,t) ((t)(uintptr_t)(a))
#endif
#define MAGIC UINT32_C(0x465a6344)
#define FN(a,t) FX_Z63_NATIVE_CALL(a,t)
static FxResult open_directory(void *context) {
    FxZ63Directory *s=context;
    if(s->uncertain || s->directory>=0)return FX_BUSY;
    s->directory=-1;
    uint32_t r=FN(0x402e695c,uint32_t (*)(const char *,int32_t *))("A:\\FENESTRA",&s->directory);
    s->native_error=r;
    if(r || s->directory<0)return r==UINT32_C(0x80000010)?FX_EMPTY:FX_IO;
    return FX_OK;
}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static FxResult next_directory(void *context,FxModuleDirectoryEntry *entry) {
    FxZ63Directory *s=context;
    if(s->uncertain || s->directory<0 || !entry)return FX_STATE;
    uint8_t record[0x220] __attribute__((aligned(8)))={0};
    uint32_t r=FN(0x402e6a0c,uint32_t (*)(int32_t,void *))(s->directory,record);
    s->native_error=r;
    if(r)return r==UINT32_C(0x80000030)?FX_EMPTY:FX_IO;
    memset(entry,0,sizeof(*entry));
    entry->attributes=record[0x20e];entry->bytes=(uint64_t)u32(record+0x218)|(uint64_t)u32(record+0x21c)<<32;
    for(uint32_t i=0;i<256u;++i) {
        uint32_t c=(uint32_t)record[14u+2u*i]|(uint32_t)record[15u+2u*i]<<8;
        if(!c) {
            if(!i)for(uint32_t j=0;j<13u && record[j];++j) {
                if(record[j]>127u){entry->name[0]=0;break;}
                entry->name[j]=(char)record[j];
            }
            /* The native short-name fallback can use uppercase. Canonicalize
             * ASCII only; unsupported names must never truncate into modules. */
            for(uint32_t j=0;j<31u && entry->name[j];++j)
                if(entry->name[j]>='A' && entry->name[j]<='Z')entry->name[j]=(char)(entry->name[j]+('a'-'A'));
            return FX_OK;
        }
        if(c>127u || i>=31u){entry->name[0]=0;return FX_OK;}
        entry->name[i]=(char)c;
    }
    return FX_INTEGRITY;
}
static FxResult close_directory(void *context) {
    FxZ63Directory *s=context;
    if(s->uncertain || s->directory<0)return FX_STATE;
    uint32_t r=FN(0x402e6a5c,uint32_t (*)(int32_t))(s->directory);s->native_error=r;
    if(r){s->uncertain=1u;return FX_CLOSE;}
    s->directory=-1;return FX_OK;
}
static FxResult read_module(void *context,const char *name,uint8_t *out,uint32_t capacity,uint32_t *bytes) {
    FxZ63Directory *s=context;char path[64]="A:\\FENESTRA\\";
    if(!bytes || !out || !capacity || capacity>FX_CARD_LIMIT+1u || !fx_service_name(name,32u,1))return FX_ARGUMENT;
    *bytes=0;
    if(s->uncertain)return FX_CLOSE;
    uint32_t n=0;while(name[n])++n;memcpy(path+12u,name,n+1u);
    int32_t fd=s->files.open(s->files.context,path,FX_TEXT_READ);
    if(fd<0)return FX_IO;
    FxResult result=FX_OK;
    for(uint32_t calls=0;*bytes<capacity;++calls) {
        if(calls>=FX_CARD_LIMIT/511u+2u){result=FX_BUDGET;break;}
        uint32_t count=0,room=capacity-*bytes;if(room>65536u)room=65536u;
        uint32_t error=s->files.read(s->files.context,fd,out+*bytes,room,&count);
        if(error || count>room){result=FX_IO;break;}
        *bytes+=count;if(!count)break;
    }
    if(s->files.close(s->files.context,fd)){s->uncertain=1u;return FX_CLOSE;}
    return result;
}
FxResult fx_z63_directory_init(FxZ63Directory *s,FxModuleDirectory *io) {
    if(!s || !io)return FX_ARGUMENT;
    if(s->magic==MAGIC && (s->directory>=0 || s->uncertain || s->card.fd>=0))return FX_BUSY;
    memset(s,0,sizeof(*s));s->magic=MAGIC;s->directory=-1;
    FxResult r=fx_z63_card_files(&s->files,&s->card);
    if(r==FX_OK)*io=(FxModuleDirectory){s,open_directory,next_directory,close_directory,read_module};
    return r;
}
