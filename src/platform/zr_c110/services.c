#include "fenestra_c110_services.h"
#include "fenestra_c110_display.h"
#include "fenestra_extension_services.h"
#include "fenestra_memory.h"
#include "fenestra_accounting_service.h"
#include "fenestra_shared_accounts.h"
#ifdef FX_PLATFORM_PROFILE
#include "fenestra_platform_native.h"
#include "fenestra_native_ui_callbacks.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_worker_wait.h"
static FxResult platform_owner(FxC110Services *s,FxPlatformOwner **out) {
    FxPlatformOwner *p=atomic_load_explicit(&s->platform,memory_order_acquire);
    if(!p) {
        p=((void *(*)(uint32_t,uint32_t))(uintptr_t)UINT64_C(0x40f41084))((uint32_t)sizeof(*p),0u);
        if(!p)return FX_BUDGET;
        if(s->accounting)(void)fx_account_acquire(&s->accounting->allocation,sizeof(*p),1u,0u);
        memset(p,0,sizeof(*p));
        FxResult r=fx_platform_init(p,fx_platform_target(),s->owner,s->generation,s->capabilities);
        if(r==FX_OK)p->capacity=fx_native_capacity_registry();
        if(r==FX_OK && s->accounting){p->accounting=&s->accounting->state;p->accounting->shared=fx_native_shared_accounts();}
        if(r==FX_OK && s->module)r=fx_platform_bind_module(p,s->module,s->module_image,s->module->bytes);
        if(r==FX_OK && s->worker_wait)r=fx_worker_wait_bind(p,s->worker_wait);
        if(r==FX_OK)r=fx_ui_dispatch_register(fx_native_ui_dispatch_domain(),p);
        if(r!=FX_OK) {
            ((void (*)(void *))(uintptr_t)UINT64_C(0x40f410c4))(p);
            if(s->accounting)(void)fx_account_release(&s->accounting->allocation,sizeof(*p),1u,0u);
            return r;
        }
        fx_account_owner_bind(p);
        if(s->accounting)(void)fx_account_retain(&s->accounting->allocation,sizeof(*p),1u);
        /* Only the original module worker publishes. Native UI reads the
         * fully initialized owner with acquire; no module supplied pointer. */
        atomic_store_explicit(&s->platform,p,memory_order_release);
    }
    *out=p;return FX_OK;
}
static FxResult platform_call(FxC110Services *s,const FxServiceRequest *q,FxServiceReply *a) {
    FxPlatformOwner *p=0;FxResult ready=platform_owner(s,&p);if(ready!=FX_OK)return ready;
    uint64_t before=fx_platform_ui_stamp(p);
    FxResult result=fx_platform_dispatch(p,q,a);
    if(fx_platform_ui_changed(p,before))
        fx_native_ui_refresh_owner(p);
    return result;
}
#endif
#ifdef FX_C110_CAPTURE_EXPERIMENT
#include "fenestra_c110_capture_menu.h"
#include "fenestra_capture_service.h"
#endif
#define FN(a,t) ((t)(uintptr_t)(a))
static FxFileObserver file_observer(void) {
#ifdef FX_PLATFORM_PROFILE
    return fx_capacity_file_observer(fx_native_capacity_registry());
#else
    return (FxFileObserver){0};
#endif
}

static int path(char out[96],const char *file) {
    if(!fx_service_name(file,32u,1))return 0;
    static const char prefix[]="A:\\FENESTRA\\";
    memcpy(out,prefix,sizeof(prefix)-1u);
    uint32_t i=0;for(;i<32u && file[i];++i)out[sizeof(prefix)-1u+i]=file[i];
    out[sizeof(prefix)-1u+i]=0;return 1;
}
FxResult fx_c110_module_read(const char *file,uint8_t *data,uint32_t capacity,uint32_t *bytes) {
    char p[96];if(!data || !bytes || !capacity || capacity>FX_NATIVE_MODULE_PACKAGE_LIMIT+1u || !path(p,file))return FX_ARGUMENT;
    *bytes=0;
    int32_t fd=FN(UINT64_C(0x402e883c),int32_t (*)(const char *,uint32_t,uint32_t))(p,0u,0u);
    FxFileObserver observer=file_observer();FxCapacityClaim claim=fx_file_observe_open(observer,fd);
    if((uint32_t)fd==UINT32_C(0x80000010))return FX_EMPTY;
    if(fd<0)return FX_IO;
    uint32_t r=0,calls=0;
    while(*bytes<capacity) {
        if(++calls>FX_READ_CALL_LIMIT) {r=1u;break;}
        uint32_t done=0,n=capacity-*bytes;
        r=FN(UINT64_C(0x402e8a0c),uint32_t (*)(int32_t,void *,uint32_t,uint32_t *))(fd,data+*bytes,n,&done);
        if(r || done>n) {r=1u;break;}
        *bytes+=done;if(!done)break;
    }
    uint32_t closed=FN(UINT64_C(0x402e889c),uint32_t (*)(int32_t))(fd);
    fx_file_observe_close(observer,&claim,closed);
    return closed?FX_CLOSE:r?FX_IO:FX_OK;
}
typedef struct { int32_t fd; } Directory;
static uint32_t directory_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static FxResult directory_open(void *context) {
    Directory *d=context;d->fd=-1;
    uint32_t r=FN(UINT64_C(0x402e8fb8),uint32_t (*)(const char *,int32_t *))("A:\\FENESTRA",&d->fd);
    if(r==UINT32_C(0x80000010))return FX_EMPTY;
    return r || d->fd<0?FX_IO:FX_OK;
}
static FxResult directory_next(void *context,FxModuleDirectoryEntry *out) {
    Directory *d=context;uint8_t data[0x220] __attribute__((aligned(8)))={0};
    uint32_t r=FN(UINT64_C(0x402e9068),uint32_t (*)(int32_t,void *))(d->fd,data);
    if(r==UINT32_C(0x80000030))return FX_EMPTY;
    if(r)return FX_IO;
    memset(out,0,sizeof(*out));out->attributes=data[0x20e];
    out->bytes=(uint64_t)directory_u32(data+0x218u)|(uint64_t)directory_u32(data+0x21cu)<<32;
    /* Native record: 14-byte SFN, then 256 UTF-16 code units. Only bounded
     * ASCII module filenames belong to this ABI; unrelated names are skipped. */
    uint32_t length=0;
    for(;length<256u;++length) {
        uint32_t ch=(uint32_t)data[14u+2u*length]|(uint32_t)data[15u+2u*length]<<8;
        if(!ch)break;
        if(length>=31u || ch>127u){out->name[0]=0;return FX_OK;}
        out->name[length]=(char)(ch>='A' && ch<='Z'?ch+('a'-'A'):ch);
    }
    if(!length)for(uint32_t i=0;i<13u && data[i];++i)out->name[i]=(char)(data[i]>='A' && data[i]<='Z'?data[i]+('a'-'A'):data[i]);
    return FX_OK;
}
static FxResult directory_close(void *context) {
    Directory *d=context;uint32_t r=FN(UINT64_C(0x402e90b8),uint32_t (*)(int32_t))(d->fd);d->fd=-1;return r?FX_CLOSE:FX_OK;
}
static FxResult directory_read(void *context,const char *file,uint8_t *data,uint32_t capacity,uint32_t *bytes) {
    (void)context;return fx_c110_module_read(file,data,capacity,bytes);
}
static void *index_allocate(void *context,uint32_t bytes) {
    (void)context;return FN(UINT64_C(0x40f41084),void *(*)(uint32_t,uint32_t))(bytes,0u);
}
static void index_release(void *context,void *memory) {
    (void)context;FN(UINT64_C(0x40f410c4),void (*)(void *))(memory);
}
FxResult fx_c110_modules_read(FxModuleInventory *out,uint8_t *scratch,uint32_t capacity,const uint8_t firmware[32]) {
    Directory d={-1};FxModuleDirectory io={&d,directory_open,directory_next,directory_close,directory_read};
    FxAccountState *shared=0;
#ifdef FX_PLATFORM_PROFILE
    shared=fx_native_shared_accounts();
#endif
    return fx_modules_discover_accounted(&io,firmware,FX_SDK_CAPS,scratch,capacity,out,(FxIndexMemory){0,index_allocate,index_release},
        fx_account_observer(fx_shared_account_row(shared,"module.indexes")),fx_account_observer(fx_shared_account_row(shared,"module.inspect")));
}
static int file_name(const FxC110Services *s,const uint8_t *data,char file[32]) {
    memcpy(file,data,32u);
    if(!fx_service_name(file,32u,1))return 0;
    uint32_t n=0;while(n<16u && s->id[n])++n;
    return n && n<16u && !memcmp(file,s->id,n) && file[n]=='-';
}
FxResult fx_c110_service_stop(void *context,uint32_t panic) {
    FxC110Services *s=context;if(!s)return FX_ARGUMENT;s->io_revoked=1u;
    if(panic)for(uint32_t i=0u;i<FX_C110_FILE_STREAMS;++i)if(s->streams[i].id)
        fx_file_observe_retain(file_observer(),&s->streams[i].claim);
#ifdef FX_PLATFORM_PROFILE
    FxPlatformOwner *p=atomic_load_explicit(&s->platform,memory_order_acquire);
    if(p){FxResult r=fx_platform_stop(p,panic);if(r!=FX_OK)return r;}
#endif
    /* Panic retains native handles: closing through an uncertain native lock
     * is not a safe substitute for stopping the camera. Normal stop may close
     * only these owned read-only card streams on their original worker. */
    if(panic) {
        return FX_OK;
    }
    for(uint32_t i=0;i<FX_C110_FILE_STREAMS;++i)if(s->streams[i].id) {
        if(s->streams[i].failed==2u)return FX_CLOSE;
        uint32_t error=FN(UINT64_C(0x402e889c),uint32_t (*)(int32_t))(s->streams[i].fd);
        fx_file_observe_close(file_observer(),&s->streams[i].claim,error);
        if(error) {s->last_native_error=error;s->streams[i].failed=2u;s->close_uncertain=1u;return FX_CLOSE;}
        memset(&s->streams[i],0,sizeof(s->streams[i]));
    }
    return FX_OK;
}
static FxResult stream_call(FxC110Services *s,const FxServiceRequest *q,FxServiceReply *a) {
    if(q->operation==FX_S_FILE_OPEN) {
        char file[32],p[96];
        if(q->bytes!=32u || q->arg[0] || q->arg[1] || q->arg[2] || q->arg[3] ||
           !file_name(s,q->data,file) || !path(p,file))return FX_ARGUMENT;
        if(s->next_stream==UINT64_MAX)return FX_BUDGET;
        FxC110FileStream *stream=0;
        for(uint32_t i=0;i<FX_C110_FILE_STREAMS;++i)if(!s->streams[i].id){stream=&s->streams[i];break;}
        if(!stream)return FX_BUDGET;
        int32_t fd=FN(UINT64_C(0x402e883c),int32_t (*)(const char *,uint32_t,uint32_t))(p,0u,0u);
        FxCapacityClaim claim=fx_file_observe_open(file_observer(),fd);
        if((uint32_t)fd==UINT32_C(0x80000010))return FX_EMPTY;
        if(fd<0){s->last_native_error=(uint32_t)fd;return FX_IO;}
        *stream=(FxC110FileStream){.id=++s->next_stream,.fd=fd,.claim=claim};
        a->value[0]=stream->id;a->value[1]=UINT64_MAX;a->value[2]=FX_SERVICE_BYTES;
        a->value[3]=FX_OVERLAY_OBJECT_LIMIT;return FX_OK;
    }
    if(q->bytes || q->arg[2] || q->arg[3] || !q->arg[0])return FX_ARGUMENT;
    FxC110FileStream *stream=0;
    for(uint32_t i=0;i<FX_C110_FILE_STREAMS;++i)if(s->streams[i].id==q->arg[0]){stream=&s->streams[i];break;}
    if(!stream)return FX_STALE;
    if(q->operation==FX_S_FILE_CLOSE) {
        if(q->arg[1])return FX_ARGUMENT;
        if(stream->failed==2u)return FX_STATE;
        uint32_t error=FN(UINT64_C(0x402e889c),uint32_t (*)(int32_t))(stream->fd);
        fx_file_observe_close(file_observer(),&stream->claim,error);
        if(error){s->last_native_error=error;stream->failed=2u;s->io_revoked=1u;s->close_uncertain=1u;return FX_CLOSE;}
        memset(stream,0,sizeof(*stream));return FX_OK;
    }
    if(q->arg[1]==0u || q->arg[1]>FX_SERVICE_BYTES)return FX_BOUNDS;
    if(stream->failed)return FX_STATE;
    uint32_t done=0;
    if(!stream->eof) {
        uint32_t error=FN(UINT64_C(0x402e8a0c),uint32_t (*)(int32_t,void *,uint32_t,uint32_t *))
            (stream->fd,a->data,(uint32_t)q->arg[1],&done);
        if(error || done>q->arg[1] || done>FX_OVERLAY_OBJECT_LIMIT-stream->position) {
            s->last_native_error=error;stream->failed=1u;return error?FX_IO:FX_BOUNDS;
        }
        stream->position+=done;if(!done)stream->eof=1u;
    }
    a->bytes=done;a->value[0]=stream->position;a->value[1]=stream->eof;return FX_OK;
}
FxResult fx_c110_service_call(void *context,const FxServiceRequest *q,FxServiceReply *a) {
    FxC110Services *s=context;
    if(s->io_revoked)return FX_STATE;
#ifdef FX_PLATFORM_PROFILE
    if(q->operation==FX_S_PLATFORM || q->operation==FX_S_UI || q->operation==FX_S_LIFECYCLE)
        return platform_call(s,q,a);
#endif
    if(q->operation==FX_S_FILE_OPEN || q->operation==FX_S_FILE_CHUNK || q->operation==FX_S_FILE_CLOSE) {
        ++s->file_calls;return stream_call(s,q,a);
    }
    if(q->operation==FX_S_MENU) {
        if(q->bytes!=32u)return FX_ARGUMENT;
        uint32_t n=0;while(n<32u && q->data[n]) {
            if(q->data[n]<32u || q->data[n]>126u)return FX_ARGUMENT;
            ++n;
        }
        if(!n || n==32u)return FX_ARGUMENT;
#ifdef FX_PLATFORM_PROFILE
        FxPlatformOwner *owner=0;FxResult ready=platform_owner(s,&owner);if(ready!=FX_OK)return ready;
#endif
        /* Label becomes immutable on first publication. Values remain atomic. */
        if(atomic_load_explicit(&s->menu_ready,memory_order_acquire)) {
            if(memcmp(s->menu_label,q->data,32u))return FX_STATE;
        } else memcpy(s->menu_label,q->data,32u);
        atomic_store(&s->menu_value,(uint32_t)q->arg[0]);
        atomic_store_explicit(&s->menu_ready,1u,memory_order_release);return FX_OK;
    }
    if(q->operation==FX_S_NATIVE) {
        if(q->arg[0]==1u) {
            if(q->bytes>=FX_SERVICE_BYTES || q->data[q->bytes])return FX_ARGUMENT;
            a->value[0]=FN(UINT64_C(0x41486740),uint64_t (*)(const char *))((const char *)q->data);
        } else if(q->arg[0]==2u) {
            FxC110View v=fx_c110_current_view();a->value[0]=v.id;a->value[1]=v.state;
        } else return FX_CAPABILITY;
        ++s->native_calls;return FX_OK;
    }
    if(q->operation==FX_S_BINDING) {
        if(q->arg[0]==FX_BINDING_SUBSTITUTION_DESCRIBE || q->arg[0]==FX_BINDING_ACCELERATOR_DESCRIBE) {
            if(q->bytes || q->arg[1] || q->arg[2] || q->arg[3])return FX_ARGUMENT;
            if(q->arg[0]==FX_BINDING_SUBSTITUTION_DESCRIBE) {
                const char text[]="fenestra.file-substitution/v1";
                memcpy(a->data,text,sizeof(text));a->bytes=sizeof(text);
                a->value[0]=FX_OVERLAY_ABI;a->value[1]=0u; /* No native targets/hooks qualified. */
                a->value[2]=FX_OVERLAY_CHUNK;a->value[3]=FX_OVERLAY_OBJECT_LIMIT;
            } else {
                FxAcceleratorDescription d={.abi=FX_ACCELERATOR_ABI,.bytes=sizeof(d),
                    .kind=FX_ACCELERATOR_XM6,.instance=UINT32_MAX,.ownership=FX_ACCELERATOR_UNKNOWN};
                static const uint8_t eg[32]={0x60,0x6d,0x61,0x16,0xdc,0x8e,0x15,0x60,
                    0xfb,0x16,0x08,0x74,0x3a,0x18,0x84,0x0e,0x0f,0x7a,0xb2,0x61,0x7d,0x11,0xbc,0xf0,
                    0x2a,0xf0,0x25,0x40,0x5a,0xca,0x15,0x42};
                memcpy(d.firmware_sha256,eg,32u);memcpy(a->data,&d,sizeof(d));a->bytes=sizeof(d);
                a->value[0]=FX_ACCELERATOR_ABI;a->value[1]=0u;
                a->value[2]=FX_ACCELERATOR_UNKNOWN;a->value[3]=sizeof(d);
            }
            return FX_OK;
        }
        /* Read-only C1.10 source-3 tables. No mode/drain qualification is
         * inferred from access to their geometry. These are not MMIO. */
        if(q->arg[0]==0u) {
            const char text[]="nikon.c110.geometry.read/v1";
            memcpy(a->data,text,sizeof(text));a->bytes=sizeof(text);a->value[0]=1u;return FX_OK;
        }
        if(q->arg[0]!=1u)return FX_CAPABILITY;
        uint64_t par;
        __asm__ volatile("at s1e1r,%1; isb; mrs %0,par_el1":"=r"(par):"r"(UINT64_C(0xa02c54178)):"memory");
        if(par&1u)return FX_CAPABILITY;
        a->value[0]=*(volatile uint16_t *)(uintptr_t)UINT64_C(0xa02c54178);
        a->value[1]=*(volatile uint16_t *)(uintptr_t)UINT64_C(0xa02c5417a);
        a->value[2]=*(volatile uint16_t *)(uintptr_t)UINT64_C(0xa02c541a4);
        a->value[3]=*(volatile uint16_t *)(uintptr_t)UINT64_C(0xa02c541a6);
        return FX_OK;
    }
    if(q->operation==FX_S_FILE_READ || q->operation==FX_S_FILE_CREATE) {
        char file[32];if(q->bytes<32u || !file_name(s,q->data,file))return FX_ARGUMENT;
        ++s->file_calls;
        if(q->operation==FX_S_FILE_READ) {
            if(q->bytes!=32u)return FX_ARGUMENT;
            uint8_t data[FX_SERVICE_BYTES+1u];uint32_t n=0;
            FxResult r=fx_c110_module_read(file,data,sizeof(data),&n);
            if(r!=FX_OK)return r;
            if(n>FX_SERVICE_BYTES)return FX_BOUNDS;
            memcpy(a->data,data,n);a->bytes=n;return FX_OK;
        }
        char p[96];if(!path(p,file) || q->bytes==32u)return FX_ARGUMENT;
        int32_t fd=FN(UINT64_C(0x402e883c),int32_t (*)(const char *,uint32_t,uint32_t))(p,0x101u,0u);
        FxFileObserver observer=file_observer();FxCapacityClaim claim=fx_file_observe_open(observer,fd);
        if(fd<0){s->last_native_error=(uint32_t)fd;return FX_IO;}
        uint32_t n=q->bytes-32u,done=0;
        uint32_t r=FN(UINT64_C(0x402e8ad0),uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *))
            (fd,q->data+32u,n,&done,0);
        uint32_t closed=FN(UINT64_C(0x402e889c),uint32_t (*)(int32_t))(fd);
        fx_file_observe_close(observer,&claim,closed);
        if(closed)return FX_CLOSE;
        if(r || done!=n)return FX_IO;
        uint8_t verify[FX_SERVICE_BYTES];uint32_t read=0;
        FxResult result=fx_c110_module_read(file,verify,n+1u,&read);
        if(result!=FX_OK)return result;
        if(read!=n || memcmp(verify,q->data+32u,n))return FX_INTEGRITY;
        a->value[0]=n;return FX_OK;
    }
    return FX_CAPABILITY;
}
