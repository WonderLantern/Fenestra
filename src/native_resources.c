#include "fenestra_resources.h"
#include "fenestra_hooks.h"
#include "fenestra_machine.h"
#include "fenestra_memory.h"
#include "fenestra_module_bus.h"
#include "fenestra_events.h"
#include "fenestra_mapping.h"
#include "fenestra_irq.h"
#include "fenestra_critical.h"
#include "fenestra_slot_pool.h"
#include "fenestra_worker_wait.h"
#include "fenestra_capacity.h"
typedef struct {void *address;uint64_t token;uint32_t bytes,released,escaped;FxCapacityClaim claim;} Buffer;
typedef struct {int32_t fd;uint32_t uncertain,mode;uint64_t id;FxCapacityClaim claim;} File;
typedef struct {int32_t id;uint32_t released,escaped;uint64_t token;uintptr_t context,set,set_irq;FxPlatformOwner *owner;FxCapacityClaim claim;} Event;
typedef struct FxNativeMappingLease Mapping;
struct FxNativeMappingLease {Mapping *next;uint64_t token,address,physical,bytes;uintptr_t table;uint32_t released,root;uint64_t writer_lock;FxCapacityClaim claim;};
typedef struct {
    uint32_t revoked;uint64_t buffer_bytes;
    FxSlotBudget budget;FxSlotPool buffer,file,event,mapping;
    FxAccountRow *metadata,*buffers,*files,*events,*mappings;
} Resources;
/* Pool retention is idempotent. All calls use the existing owner writer lock;
 * observing the delta avoids adding per-object bookkeeping or IRQ work. */
static FxResult retain_object(FxSlotPool *pool,uint64_t token,FxAccountRow *row,uint64_t bytes) {
    uint32_t before=pool->retained;FxResult r=fx_slot_pool_retain(pool,token);
    if(r==FX_OK && row && pool->retained!=before)(void)fx_account_retain(row,bytes,1u);
    return r;
}
static FxResult release_object(FxSlotPool *pool,uint64_t token,FxAccountRow *row,uint64_t bytes) {
    uint32_t before=pool->retained;FxResult r=fx_slot_pool_release(pool,token);
    if(r==FX_OK && row)(void)fx_account_release(row,bytes,1u,(uint32_t)(pool->retained!=before));
    return r;
}
static void acquired(FxAccountRow *row,uint64_t bytes) {if(row)(void)fx_account_acquire(row,bytes,1u,0u);}
static void row(FxPlatformOwner *o,const char *name,uint32_t scope,uint64_t budget,FxAccountRow **out) {
    if(o->accounting)(void)fx_account_get(o->accounting,name,scope,budget,out);
}
static int same(const char *a,const char *b) {
    for(uint32_t i=0;i<64u;++i){if(a[i]!=b[i])return 0;if(!a[i])return 1;}return 0;
}
/* The dispatcher holds binding_lock during attach/detach. These services own
 * their streams/buffers/endpoints per owner; an independent binding handle is
 * a reference to that owner's service, not ownership of every live object. */
static int other_binding(const FxPlatformOwner *o,const FxNamedBinding *b,uint64_t token) {
    for(uint32_t i=0;i<o->bindings.lease_capacity;++i) {
        const FxBindingLease *l=fx_binding_store_lease_at(&o->bindings,i);
        if(l && l->token!=token && l->attached==1u && fx_platform_binding(o,l->index)==b)return 1;
    }
    return 0;
}
static uintptr_t native(const FxPlatformOwner *o,const char *name) {
    const FxNamedBinding *b=fx_platform_find(o,name);
    return b && b->kind==FX_BIND_FUNCTION && b->abi==FX_CALL_U64_8?(uintptr_t)b->address:0;
}
/* Keep the full copied call frame off unrelated file/event/memory paths. */
static __attribute__((noinline)) FxResult critical_call(FxPlatformOwner *o,uint32_t op,uint64_t *cookie,
    const FxServiceRequest *q,FxServiceReply *a) {
    if(op==FX_RESOURCE_ATTACH){*cookie=1u;return FX_OK;}
    if(op==FX_RESOURCE_DETACH){*cookie=0u;return FX_OK;}
    if(op==FX_RESOURCE_PANIC)return FX_OK;
    if(op!=FX_RESOURCE_CALL || !q || !a || !*cookie)return FX_STATE;
    if(q->arg[3])return FX_ARGUMENT;
    if(q->arg[2]==FX_CRITICAL_QUERY) {
        if(q->bytes)return FX_ARGUMENT;
        FxCriticalInfo info={1u,sizeof(info),3u,16u};
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(q->arg[2]!=FX_CRITICAL_CALL || q->bytes!=sizeof(FxCriticalCall))return FX_ARGUMENT;
    FxCriticalCall call;memcpy(&call,q->data,sizeof(call));
    if(!call.lock || (call.lock&7u) || call.reserved)return FX_ARGUMENT;
    FxNamedBinding fn;FxResult r=fx_platform_prepare_native(o,call.binding,call.call,call.call_bytes,&fn);
    if(r!=FX_OK)return r;
    uintptr_t acquire=native(o,"rtos.spinlock.try-lock"),release=native(o,"rtos.spinlock.unlock");
    if(!acquire || !release)return FX_CAPABILITY;
    /* Never return/yield to a module with IRQ disabled and a CPU lock held.
     * Do not hold a lock across copied-message calls, even on one worker. */
    int32_t acquired=((int32_t (*)(uintptr_t))acquire)((uintptr_t)call.lock);
    a->value[2]=(uint64_t)(int64_t)acquired;
    if(acquired)return acquired==-1009?FX_BUSY:FX_IO;
    uint64_t values[2];r=fx_platform_invoke_native(o,&fn,call.call,call.call_bytes,values);
    int32_t released=((int32_t (*)(uintptr_t))release)((uintptr_t)call.lock);
    a->value[0]=values[0];a->value[1]=values[1];a->value[3]=(uint64_t)(int64_t)released;
    if(fn.abi==FX_CALL_AAPCS64){memcpy(a->data,call.call,sizeof(FxNativeCallFrame));a->bytes=sizeof(FxNativeCallFrame);}
    return released?FX_IO:r;
}
static void *resource_allocate(void *context,uint32_t bytes) {
    uintptr_t fn=native(context,"rtos.memory.allocate");
    return fn?((void *(*)(uint32_t,uint32_t))fn)(bytes,0u):0;
}
static void resource_release(void *context,void *memory) {
    ((void (*)(void *))native(context,"rtos.memory.release"))(memory);
}
static FxResult resource_slot(FxPlatformOwner *o,FxSlotPool *pool,void **out,uint64_t *token) {
    if(!native(o,"rtos.memory.release"))return FX_CAPABILITY;
    return fx_slot_pool_take(pool,out,token);
}
static Resources *resources(FxPlatformOwner *o) {
    if(o->native_resources)return o->native_resources;
    uintptr_t a=native(o,"rtos.memory.allocate");if(!a)return 0;
    Resources *s=((void *(*)(uint32_t,uint32_t))a)((uint32_t)sizeof(*s),0u);if(!s)return 0;
    memset(s,0,sizeof(*s));s->budget=(FxSlotBudget){FX_NATIVE_RESOURCE_BUDGET,sizeof(*s),sizeof(*s)};
    FxIndexMemory memory={o,resource_allocate,resource_release};
    (void)fx_slot_pool_init(&s->buffer,memory,&s->budget,sizeof(Buffer));
    (void)fx_slot_pool_init(&s->file,memory,&s->budget,sizeof(File));
    (void)fx_slot_pool_init(&s->event,memory,&s->budget,sizeof(Event));
    (void)fx_slot_pool_init(&s->mapping,memory,&s->budget,sizeof(Mapping));
    row(o,"resource.metadata",FX_ACCOUNT_MANAGED,FX_NATIVE_RESOURCE_BUDGET,&s->metadata);
    row(o,"memory.buffers",FX_ACCOUNT_MANAGED,FX_NATIVE_BUFFER_BUDGET,&s->buffers);
    row(o,"card.streams",FX_ACCOUNT_EXTERNAL,0u,&s->files);
    row(o,"rtos.events",FX_ACCOUNT_EXTERNAL,0u,&s->events);
    row(o,"mmu.claimed-spans",FX_ACCOUNT_VIEW,0u,&s->mappings);
    if(s->metadata) {
        (void)fx_account_acquire(s->metadata,sizeof(*s),1u,0u);
        FxAllocationObserver observer=fx_account_observer(s->metadata);
        s->buffer.observer=s->file.observer=s->event.observer=s->mapping.observer=observer;
        (void)fx_account_retain(s->metadata,sizeof(*s),1u);
    }
    o->native_resources=s;return s;
}
#include "native_events.inc"
#include "native_irq.inc"
#include "native_mapping.inc"
static FxResult memory_call(FxPlatformOwner *o,Resources *s,const FxServiceRequest *q,FxServiceReply *a) {
    if(q->arg[2]==FX_MEMORY_QUERY) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        FxMemoryInfo info={.abi=1u,.bytes=sizeof(info),.operations=0x1eu,.max_buffers=fx_slot_pool_limit(&s->buffer),.max_bytes=FX_NATIVE_BUFFER_MAX_BYTES};
        if(native(o,"rtos.memory.release"))info.operations|=0xe0u;
        if(o->module_image)info.operations|=(1u<<FX_MEMORY_FROM_MODULE)|(1u<<FX_MEMORY_TO_MODULE);
        info.retained_buffers=s->buffer.retained;info.active_buffers=s->buffer.used-s->buffer.retained;
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);a->value[0]=info.abi;return FX_OK;
    }
    if(q->bytes<sizeof(FxMemoryRequest))return FX_ARGUMENT;
    FxMemoryRequest r;memcpy(&r,q->data,sizeof(r));uint64_t op=q->arg[2];
    if(op==FX_MEMORY_ALLOCATE || op==FX_MEMORY_COPY_ALLOCATE) {
        if(q->bytes!=sizeof(r) || !r.bytes || r.bytes>FX_NATIVE_BUFFER_MAX_BYTES ||
           r.bytes>FX_NATIVE_BUFFER_BUDGET-s->buffer_bytes || r.handle || r.offset)return FX_BUDGET;
        uintptr_t alloc=native(o,"rtos.memory.allocate");if(!alloc)return FX_CAPABILITY;
        if(op==FX_MEMORY_COPY_ALLOCATE && !native(o,"rtos.memory.release"))return FX_CAPABILITY;
        void *item=0;uint64_t handle=0u;FxResult result=resource_slot(o,&s->buffer,&item,&handle);Buffer *b=item;
        if(result!=FX_OK)return result;
        void *p=((void *(*)(uint32_t,uint32_t))alloc)(r.bytes,0u);
        FxCapacityDomain *domain=fx_capacity_find(o->capacity,"rtos.memory.buffers.bytes");
        if(!p){fx_capacity_failed(domain,0u);(void)fx_slot_pool_release(&s->buffer,handle);return FX_BUDGET;}
        memset(p,0,r.bytes);*b=(Buffer){.address=p,.token=handle,.bytes=r.bytes,.escaped=(uint32_t)(op==FX_MEMORY_ALLOCATE),
            .claim=fx_capacity_acquire(domain,r.bytes)};
        acquired(s->buffers,r.bytes);
        s->buffer_bytes+=r.bytes;
        a->value[0]=b->token;a->value[1]=b->escaped?(uintptr_t)p:0u;a->value[2]=r.bytes;return FX_OK;
    }
    Buffer *b=fx_slot_pool_get(&s->buffer,r.handle);
    if(!b)return FX_STALE;
    if(op==FX_MEMORY_RECLAIM || (op==FX_MEMORY_RELEASE && !b->escaped)) {
        if(q->bytes!=sizeof(r) || r.bytes || r.offset)return FX_ARGUMENT;
        uintptr_t release=native(o,"rtos.memory.release");if(!release)return FX_CAPABILITY;
        ((void (*)(void *))release)(b->address);s->buffer_bytes-=b->bytes;
        fx_capacity_release(&b->claim,0u);
        return release_object(&s->buffer,b->token,s->buffers,b->bytes);
    }
    if(b->released)return FX_STALE;
    if(op==FX_MEMORY_FROM_MODULE || op==FX_MEMORY_TO_MODULE) {
        if(q->bytes!=sizeof(FxModuleMemoryRequest) || q->arg[3])return FX_ARGUMENT;
        FxModuleMemoryRequest bulk;memcpy(&bulk,q->data,sizeof(bulk));
        if(!bulk.bytes || bulk.offset>b->bytes || bulk.bytes>b->bytes-bulk.offset)return FX_BOUNDS;
        FxResult result=fx_platform_module_transfer(o,bulk.module_address,(uint8_t *)b->address+bulk.offset,
            bulk.bytes,(uint32_t)(op==FX_MEMORY_TO_MODULE));
        if(result==FX_OK)a->value[0]=bulk.bytes;
        return result;
    }
    if(op==FX_MEMORY_EXPORT) {
        if(q->bytes!=sizeof(r) || r.bytes || r.offset)return FX_ARGUMENT;
        b->escaped=1u;a->value[0]=b->token;a->value[1]=(uintptr_t)b->address;a->value[2]=b->bytes;return FX_OK;
    }
    if(op==FX_MEMORY_RELEASE) {
        if(q->bytes!=sizeof(r) || r.bytes || r.offset)return FX_ARGUMENT;
        /* Native escape can hand this address to asynchronous Nikon code.
         * Revoke copied access, but never free/reuse it before reboot. */
        b->released=1u;fx_capacity_retain(&b->claim);return retain_object(&s->buffer,b->token,s->buffers,b->bytes);
    }
    if(!r.bytes || r.offset>b->bytes || r.bytes>b->bytes-r.offset || r.bytes>FX_SERVICE_BYTES-sizeof(r))return FX_BOUNDS;
    uint8_t *p=(uint8_t *)b->address+r.offset;
    if(op==FX_MEMORY_READ && q->bytes==sizeof(r)){memcpy(a->data,p,r.bytes);a->bytes=r.bytes;return FX_OK;}
    if(op==FX_MEMORY_WRITE && q->bytes==sizeof(r)+r.bytes){memcpy(p,q->data+sizeof(r),r.bytes);return FX_OK;}
    return FX_ARGUMENT;
}
static FxResult close_file(FxPlatformOwner *o,Resources *s,File *f,FxServiceReply *a) {
    if(!f)return FX_OK;
    if(f->uncertain)return FX_CLOSE;
    uintptr_t cl=native(o,"camera.card.close");if(!cl)return FX_CAPABILITY;
    uint32_t r=((uint32_t (*)(int32_t))cl)(f->fd);
    if(a)a->value[3]=r;
    fx_capacity_release(&f->claim,r);
    if(r){f->uncertain=1u;(void)retain_object(&s->file,f->id,s->files,0u);return FX_CLOSE;}
    return release_object(&s->file,f->id,s->files,0u);
}
static FxResult file_call(FxPlatformOwner *o,Resources *s,const FxServiceRequest *q,FxServiceReply *a) {
    if(q->arg[2]==FX_FILE_SEEK) {
        if(q->bytes!=sizeof(FxFileSeekRequest))return FX_ARGUMENT;
        FxFileSeekRequest seek;memcpy(&seek,q->data,sizeof(seek));
        if(seek.whence>FX_SEEK_END || seek.reserved)return FX_ARGUMENT;
        File *file=fx_slot_pool_get(&s->file,seek.handle);
        if(!file)return FX_STALE;
        if(file->uncertain)return FX_CLOSE;
        uintptr_t fn=native(o,"camera.card.seek64");if(!fn)return FX_CAPABILITY;
        uint64_t position=0;
        /* This native API returns BOOL, unlike read/write's zero-success
         * status. Preserve all 64 offset/position bits and the native result. */
        uint32_t success=((uint32_t (*)(int32_t,int64_t,uint32_t,uint64_t *))fn)
            (file->fd,seek.offset,seek.whence,&position);
        a->value[3]=success;
        if(!success)return FX_IO;
        a->value[0]=position;return FX_OK;
    }
    if(q->bytes<sizeof(FxFileRequest))return FX_ARGUMENT;
    FxFileRequest r;memcpy(&r,q->data,sizeof(r));uint64_t op=q->arg[2];
    if(op==FX_FILE_OPEN) {
        if(q->bytes!=sizeof(r) || r.handle || r.mode>FX_FILE_CREATE_EXCLUSIVE || !fx_service_name(r.name,64u,1))return FX_ARGUMENT;

        char path[80]={0};memcpy(path,"A:\\FENESTRA\\",12u);uint32_t n=0;while(r.name[n])++n;memcpy(path+12u,r.name,n+1u);
        uintptr_t fn=native(o,"camera.card.open");if(!fn)return FX_CAPABILITY;
        void *item=0;uint64_t handle=0u;FxResult result=resource_slot(o,&s->file,&item,&handle);File *f=item;
        if(result!=FX_OK)return result;
        int32_t fd=((int32_t (*)(const char *,uint32_t,uint32_t))fn)(path,r.mode==FX_FILE_READ_ONLY?0u:0x101u,0u);
        FxCapacityClaim claim=fx_capacity_file_open(fx_capacity_find(o->capacity,"camera.card.streams.a"),fd);
        if(fd<0){(void)fx_slot_pool_release(&s->file,handle);a->value[3]=(uint32_t)fd;return (uint32_t)fd==UINT32_C(0x80000010)?FX_EMPTY:FX_IO;}
        *f=(File){.fd=fd,.mode=r.mode,.id=handle,.claim=claim};acquired(s->files,0u);a->value[0]=f->id;return FX_OK;
    }
    File *f=fx_slot_pool_get(&s->file,r.handle);
    if(!f)return FX_STALE;
    if(f->uncertain)return FX_CLOSE;
    if(op==FX_FILE_CLOSE)return q->bytes==sizeof(r)?close_file(o,s,f,a):FX_ARGUMENT;
    if(!r.bytes || r.bytes>FX_SERVICE_BYTES-sizeof(r))return FX_BOUNDS;
    uint32_t done=0,result;
    if(op==FX_FILE_READ && q->bytes==sizeof(r) && f->mode==FX_FILE_READ_ONLY) {
        uintptr_t fn=native(o,"camera.card.read");if(!fn)return FX_CAPABILITY;
        result=((uint32_t (*)(int32_t,void *,uint32_t,uint32_t *))fn)(f->fd,a->data,r.bytes,&done);
        if(done>r.bytes){f->uncertain=1u;fx_capacity_retain(&f->claim);(void)retain_object(&s->file,f->id,s->files,0u);return FX_INTEGRITY;}
        if(!result)a->bytes=done;
    } else if(op==FX_FILE_WRITE && q->bytes==sizeof(r)+r.bytes && f->mode==FX_FILE_CREATE_EXCLUSIVE) {
        uintptr_t fn=native(o,"camera.card.write");if(!fn)return FX_CAPABILITY;
        result=((uint32_t (*)(int32_t,const void *,uint32_t,uint32_t *,void *))fn)(f->fd,q->data+sizeof(r),r.bytes,&done,0);
        if(done>r.bytes){f->uncertain=1u;fx_capacity_retain(&f->claim);(void)retain_object(&s->file,f->id,s->files,0u);return FX_INTEGRITY;}
    } else return FX_ARGUMENT;
    a->value[0]=done;a->value[3]=result;
    /* Short IO is reported exactly; caller decides whether to continue. */
    return result?FX_IO:FX_OK;
}
FxResult fx_native_resource(void *context,uint32_t op,const FxNamedBinding *b,FxPlatformOwner *o,uint64_t token,
    uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)token;
    if(same(b->name,"core.schedule"))return fx_schedule_resource(op,o,cookie,q,a);
    if(same(b->name,"core.machine"))return fx_machine_resource(context,op,b,o,token,cookie,q,a);
    if(same(b->name,"core.critical"))return critical_call(o,op,cookie,q,a);
    if(same(b->name,"core.hooks"))return fx_hook_resource(context,op,b,o,token,cookie,q,a);
    if(same(b->name,"core.services")) {
        FxModuleBus *bus=fx_native_module_bus();if(!bus)return FX_BUDGET;
        if(op==FX_RESOURCE_ATTACH){
            if(!native(o,"rtos.memory.allocate") || !native(o,"rtos.memory.release"))return FX_CAPABILITY;
            FxResult r=fx_module_bus_init(bus,(FxIndexMemory){o,resource_allocate,resource_release});
            if(r==FX_OK)*cookie=(uintptr_t)bus;
            return r;
        }
        if(*cookie!=(uintptr_t)bus)return FX_STALE;
        if(op==FX_RESOURCE_DETACH && other_binding(o,b,token)){*cookie=0;return FX_OK;}
        if(op==FX_RESOURCE_DETACH || op==FX_RESOURCE_PANIC) {
            FxResult r=fx_module_bus_stop(bus,o);if(r==FX_OK && op==FX_RESOURCE_DETACH)*cookie=0;return r;
        }
        return op==FX_RESOURCE_CALL && q && a?fx_module_bus_call(bus,o,q,a):FX_ARGUMENT;
    }
    int tick=same(b->name,"core.tick"),memory=same(b->name,"core.memory"),files=same(b->name,"card.files"),events=same(b->name,"rtos.events"),mappings=same(b->name,"rtos.mappings");
    if(!tick && !memory && !files && !events && !mappings)return FX_CAPABILITY;
    if(tick) {
        if(op==FX_RESOURCE_ATTACH){if(!*cookie)atomic_fetch_add(&o->tick_subscriptions,1u);*cookie=1u;return FX_OK;}
        if(op==FX_RESOURCE_DETACH){if(*cookie)atomic_fetch_sub(&o->tick_subscriptions,1u);*cookie=0;return FX_OK;}
        return op==FX_RESOURCE_PANIC?FX_OK:FX_ARGUMENT;
    }
    Resources *s=resources(o);if(!s)return FX_BUDGET;
    if(op==FX_RESOURCE_ATTACH){if(s->revoked)return FX_STATE;*cookie=(uintptr_t)s;return FX_OK;}
    if(*cookie!=(uintptr_t)s)return FX_STALE;
    if(op==FX_RESOURCE_PANIC) {
        s->revoked=1u;
        for(uint32_t i=0u;i<s->buffer.issued;++i){Buffer *p=fx_slot_pool_at(&s->buffer,i);if(p){fx_capacity_retain(&p->claim);(void)retain_object(&s->buffer,p->token,s->buffers,p->bytes);}}
        for(uint32_t i=0u;i<s->file.issued;++i){File *p=fx_slot_pool_at(&s->file,i);if(p){fx_capacity_retain(&p->claim);(void)retain_object(&s->file,p->id,s->files,0u);}}
        for(uint32_t i=0u;i<s->event.issued;++i){Event *p=fx_slot_pool_at(&s->event,i);if(p){fx_capacity_retain(&p->claim);(void)retain_object(&s->event,p->token,s->events,0u);}}
        for(uint32_t i=0u;i<s->mapping.issued;++i){Mapping *p=fx_slot_pool_at(&s->mapping,i);if(p){fx_capacity_retain(&p->claim);(void)retain_object(&s->mapping,p->token,s->mappings,p->bytes);}}
        return FX_OK;
    }
    if(op==FX_RESOURCE_DETACH) {
        if(other_binding(o,b,token)){*cookie=0;return FX_OK;}
        if(mappings)for(uint32_t i=0u;i<s->mapping.issued;++i){Mapping *m=fx_slot_pool_at(&s->mapping,i);if(m){m->released=1u;fx_capacity_retain(&m->claim);(void)retain_object(&s->mapping,m->token,s->mappings,m->bytes);}}
        if(memory)for(uint32_t i=0u;i<s->buffer.issued;++i) {
            Buffer *buffer=fx_slot_pool_at(&s->buffer,i);
            if(buffer && !buffer->escaped) {
                uintptr_t release=native(o,"rtos.memory.release");if(!release)return FX_CAPABILITY;
                ((void (*)(void *))release)(buffer->address);s->buffer_bytes-=buffer->bytes;
                fx_capacity_release(&buffer->claim,0u);
                (void)release_object(&s->buffer,buffer->token,s->buffers,buffer->bytes);
            } else if(buffer){fx_capacity_retain(&buffer->claim);(void)retain_object(&s->buffer,buffer->token,s->buffers,buffer->bytes);}
        }
        if(files)for(uint32_t i=0u;i<s->file.issued;++i){FxResult r=close_file(o,s,fx_slot_pool_at(&s->file,i),0);if(r!=FX_OK)return r;}
        if(events)for(uint32_t i=0u;i<s->event.issued;++i) {
            Event *e=fx_slot_pool_at(&s->event,i);if(!e)continue;
            if(e->escaped){e->released=1u;fx_capacity_retain(&e->claim);(void)retain_object(&s->event,e->token,s->events,0u);continue;}
            FxResult r=event_delete(o,s,e,0);if(r!=FX_OK)return r;
        }
        *cookie=0;return FX_OK;
    }
    if(op!=FX_RESOURCE_CALL || !q || !a || s->revoked)return FX_STATE;
    return mappings?mapping_call(o,s,q,a):memory?memory_call(o,s,q,a):events?event_call(o,s,q,a):file_call(o,s,q,a);
}
void fx_platform_pulse(FxPlatformOwner *o,uint64_t time_ms) {
    if(!o || !o->platform || o->phase!=FX_OWNER_RUNNING)return;
    uint32_t zero=0u,queued=0u;
    if(!atomic_compare_exchange_strong_explicit(&o->binding_lock,&zero,1u,memory_order_acquire,memory_order_relaxed))return;
    for(uint32_t i=0;i<o->bindings.lease_capacity;++i) {
        FxBindingLease *l=fx_binding_store_lease_at(&o->bindings,i);
        if(l && l->attached==1u && same(fx_platform_binding(o,l->index)->name,"core.tick"))
            if(fx_platform_notify_locked(o,l->token,FX_HOOK_TICK,(uint32_t)time_ms)==FX_OK)queued=1u;
    }
    atomic_store_explicit(&o->binding_lock,0u,memory_order_release);
    if(queued)fx_platform_wake(o);
}
