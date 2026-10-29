#include "fenestra_services.h"
#include "fenestra_platform.h"
#include "fenestra_extension_services.h"
#include "fenestra_memory.h"
_Static_assert(sizeof(FxServiceRequest)==816u,"service request wire layout");
_Static_assert(sizeof(FxServiceReply)==816u,"service reply wire layout");
_Static_assert(sizeof(FxServiceExchange)<=4096u,"service exchange page");

int fx_service_name(const char *s,uint32_t n,int dot) {
    if(!s || !n || !s[0])return 0;
    uint32_t i=0;
    for(;i<n && s[i];++i) {
        char c=s[i];
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || (dot && c=='.')))return 0;
        if(c=='.' && (!i || s[i-1u]=='.'))return 0;
    }
    if(i==n || s[i-1u]=='.')return 0;
    for(;i<n;++i)if(s[i])return 0;
    return 1;
}
FxResult fx_module_directory_inspect(const FxModuleDirectory *io,const char *name,uint64_t size,
        const uint8_t firmware[32],uint64_t capabilities,FxIndexMemory memory,uint8_t *scratch,
        uint32_t capacity,FxModule *out,uint8_t digest[32]) {
    return fx_module_directory_inspect_accounted(io,name,size,firmware,capabilities,memory,scratch,capacity,out,digest,(FxAllocationObserver){0});
}
FxResult fx_module_directory_inspect_accounted(const FxModuleDirectory *io,const char *name,uint64_t size,
        const uint8_t firmware[32],uint64_t capabilities,FxIndexMemory memory,uint8_t *scratch,
        uint32_t capacity,FxModule *out,uint8_t digest[32],FxAllocationObserver observer) {
    if(!io || !io->read || !firmware || !out || !digest || !name)return FX_ARGUMENT;
    char copied_name[32]={0};uint32_t name_bytes=0;
    while(name_bytes<31u && name[name_bytes]){copied_name[name_bytes]=name[name_bytes];++name_bytes;}
    if(name[name_bytes] || !fx_service_name(copied_name,32u,1))return FX_ARGUMENT;
    if(size<=FX_MODULE_HEADER || size>FX_NATIVE_MODULE_PACKAGE_LIMIT)return FX_BOUNDS;
    uint32_t room=(uint32_t)size+1u;uint8_t *buffer=scratch;int temporary=0;
    if(!buffer || capacity<room) {
        if(!memory.allocate || !memory.release)return FX_ARGUMENT;
        buffer=memory.allocate(memory.context,room);if(!buffer)return FX_BUDGET;
        fx_allocation_observe(observer,room,FX_ALLOC_REQUESTED);
        temporary=1;
    }
    uint32_t bytes=0;FxModule module;
    FxResult r=io->read(io->context,copied_name,buffer,room,&bytes);
    if(r==FX_OK && bytes!=size)r=FX_INTEGRITY;
    if(r==FX_OK && fx_module_header_entry(buffer,bytes)!=FX_MODULE_ENTRY_SERVICES)r=FX_CAPABILITY;
    if(r==FX_OK)r=fx_module_inspect(buffer,bytes,firmware,capabilities,FX_NATIVE_MODULE_IMAGE_LIMIT,&module);
    if(r==FX_OK){fx_sha256(buffer,bytes,digest);*out=module;}
    if(temporary){memory.release(memory.context,buffer);fx_allocation_observe(observer,room,FX_ALLOC_FREE_PENDING);}
    return r;
}
void fx_modules_destroy(FxModuleInventory *inventory) {
    if(!inventory)return;
    fx_module_index_destroy(&inventory->index);memset(inventory,0,sizeof(*inventory));
}
FxResult fx_modules_discover(const FxModuleDirectory *io,const uint8_t firmware[32],
        uint32_t capabilities,uint8_t *scratch,uint32_t capacity,FxModuleInventory *out,FxIndexMemory memory) {
    return fx_modules_discover_accounted(io,firmware,capabilities,scratch,capacity,out,memory,(FxAllocationObserver){0},(FxAllocationObserver){0});
}
FxResult fx_modules_discover_accounted(const FxModuleDirectory *io,const uint8_t firmware[32],
        uint32_t capabilities,uint8_t *scratch,uint32_t capacity,FxModuleInventory *out,FxIndexMemory memory,
        FxAllocationObserver indexes,FxAllocationObserver packages) {
    if(!out)return FX_ARGUMENT;
    if(out->count || out->entries || out->index.records || out->index.buckets)return FX_BUSY;
    if(!io || !io->open || !io->next || !io->close || !io->read || !firmware || !scratch || capacity<FX_MODULE_HEADER+1u)return FX_ARGUMENT;
    FxModuleIndex seen;
    FxResult result=fx_module_index_init(&seen,memory,16u,FX_MODULE_INDEX_BUDGET);
    if(result!=FX_OK)return result;
    (void)fx_module_index_account(&seen,indexes);
    FxModuleInventory found={0};uint32_t exhausted=0;
    result=fx_module_index_init(&found.index,memory,(uint32_t)sizeof(FxDiscoveredModule),FX_MODULE_INDEX_BUDGET);
    if(result!=FX_OK)return result;
    (void)fx_module_index_account(&found.index,indexes);
    result=io->open(io->context);
    if(result==FX_EMPTY)return FX_OK;
    if(result!=FX_OK)return result;
    for(uint32_t attempt=0;attempt<65536u;++attempt) {
        FxModuleDirectoryEntry item={0};result=io->next(io->context,&item);
        if(result==FX_EMPTY){result=FX_OK;exhausted=1u;break;}
        if(result!=FX_OK)break;
        if(item.attributes&0x18u || !item.name[0])continue;
        uint32_t n=0;while(n<32u && item.name[n])++n;
        uint32_t disabled=n>9u && !memcmp(item.name+n-9u,".disabled",9u);
        uint32_t end=n-(disabled?9u:0u);
        if(end<5u || memcmp(item.name+end-4u,".wlm",4u))continue;
        if(!fx_service_name(item.name,32u,1)){result=FX_ARGUMENT;break;}
        uint32_t id_bytes=end-4u;
        if(id_bytes>=16u){result=FX_ARGUMENT;break;}
        char identity[16]={0};memcpy(identity,item.name,id_bytes);
        if(!fx_service_name(identity,16u,0)){result=FX_ARGUMENT;break;}
        uint32_t identity_slot;
        result=fx_module_index_add(&seen,identity,&identity_slot);
        if(result!=FX_OK)break;
        /* Keep duplicate checks across every filename, but allocate runnable
         * owner slots only for enabled files. No disabled package read. */
        if(disabled)continue;
        FxDiscoveredModule descriptor={0};
        memcpy(descriptor.id,identity,16u);memcpy(descriptor.file,item.name,32u);
        descriptor.enabled=1u;descriptor.period_ms=33u;descriptor.budget_ms=100u;
        FxModule module;
        result=fx_module_directory_inspect_accounted(io,item.name,item.bytes,firmware,capabilities,memory,scratch,capacity,&module,descriptor.digest,packages);
        if(result!=FX_OK)break;
        uint32_t slot;
        result=fx_module_index_add(&found.index,identity,&slot);if(result!=FX_OK)break;
        descriptor.bytes=(uint32_t)item.bytes;descriptor.image_bytes=module.bytes;
        descriptor.required=(uint32_t)module.required_capabilities;descriptor.stacks=module.stacks;
        found.entries=found.index.records;found.entries[slot]=descriptor;found.count=found.index.count;
    }
    FxResult closed=io->close(io->context);
    fx_module_index_destroy(&seen);
    if(closed!=FX_OK)result=closed;
    if(result==FX_OK && !exhausted)result=FX_BUDGET;
    if(result!=FX_OK){fx_modules_destroy(&found);return result;}
    *out=found;return FX_OK;
}
void fx_service_init(FxServiceOwner *s,uint32_t owner,uint32_t generation,uint32_t caps,FxServiceBackend backend) {
    memset(s,0,sizeof(*s));s->owner=owner;s->generation=generation;s->capabilities=caps;
    s->active=1u;s->backend=backend;s->revision=1u;
}
FxResult fx_service_region(FxServiceOwner *s,const char *name,void *memory,uint32_t bytes,uint32_t flags,uint32_t *id) {
    if(!s || !s->active || !memory || !bytes || !id || !name || s->region_count==FX_SERVICE_REGIONS ||
       !(flags&FX_REGION_READ) || (flags&~7u) || ((flags&FX_REGION_PATCH) && !(flags&FX_REGION_WRITE)) ||
       bytes>UINTPTR_MAX-(uintptr_t)memory)return FX_ARGUMENT;
    uint32_t n=0;while(n<32u && name[n])++n;if(!n || n==32u)return FX_ARGUMENT;
    for(uint32_t i=0;i<s->region_count;++i) {
        uintptr_t a=(uintptr_t)memory,b=(uintptr_t)s->regions[i].data;
        if(bytes>UINTPTR_MAX-a || s->regions[i].bytes>UINTPTR_MAX-b ||
           (a<b+s->regions[i].bytes && b<a+bytes))return FX_ARGUMENT;
    }
    FxServiceRegion *r=&s->regions[s->region_count++];
    r->data=memory;r->bytes=bytes;r->flags=flags;r->id=(s->owner<<16)|s->region_count;
    memcpy(r->name,name,n);*id=r->id;return FX_OK;
}
FxResult fx_service_event(FxServiceOwner *s,uint32_t event,uint32_t value) {
    if(!s || !s->active)return FX_STATE;
    /* VIEW is a latest-state notification, not an execution command or a
     * trace stream. Unsubscribed SDK modules need not drain camera views. */
    if(event==FX_EVENT_VIEW)for(uint32_t i=0;i<s->events_count;++i) {
        uint32_t at=(s->events_head+i)%FX_SERVICE_EVENTS;
        if(s->events[at][0]==FX_EVENT_VIEW){
            if(s->events[at][1]!=value){s->events[at][1]=value;if(s->wake_callback)s->wake_callback(s->wake_context);}
            return FX_OK;
        }
    }
    if(s->events_count==FX_SERVICE_EVENTS)return FX_BUSY;
    uint32_t at=(s->events_head+s->events_count)%FX_SERVICE_EVENTS;
    s->events[at][0]=event;s->events[at][1]=value;++s->events_count;
    if(s->wake_callback)s->wake_callback(s->wake_context);
    return FX_OK;
}
static FxServiceRegion *region(FxServiceOwner *s,uint64_t id) {
    for(uint32_t i=0;i<s->region_count;++i)if(s->regions[i].id==id)return &s->regions[i];
    return 0;
}
static FxResult restore(FxServiceOwner *s) {
    if(!s->patch_committed)return FX_STATE;
    for(uint32_t i=0;i<s->patch_count;++i) {
        FxServicePatch *p=&s->patches[i];FxServiceRegion *r=region(s,p->region);
        if(!r || memcmp(r->data+p->offset,p->after,p->bytes)) {fx_service_fault(s);return FX_INTEGRITY;}
    }
    for(uint32_t i=s->patch_count;i>0u;--i) {
        FxServicePatch *p=&s->patches[i-1u];FxServiceRegion *r=region(s,p->region);
        memcpy(r->data+p->offset,p->before,p->bytes);
    }
    s->patch_count=s->patch_committed=0u;++s->revision;return FX_OK;
}
static FxResult execute(FxServiceOwner *s,const FxServiceRequest *q,FxServiceReply *a) {
    uint32_t cap=0;
    switch(q->operation) {
    case FX_S_QUERY:case FX_S_LOG:case FX_S_EVENT:case FX_S_PANIC:cap=FX_SDK_QUERY;break;
    case FX_S_REGION_INFO:case FX_S_READ:case FX_S_WRITE:case FX_S_PATCH_PREPARE:
    case FX_S_PATCH_COMMIT:case FX_S_PATCH_RESTORE:case FX_S_PATCH_ABORT:cap=FX_SDK_MEMORY;break;
    case FX_S_FILE_READ:case FX_S_FILE_CREATE:cap=FX_SDK_FILES;break;
    case FX_S_FILE_OPEN:case FX_S_FILE_CHUNK:case FX_S_FILE_CLOSE:cap=FX_SDK_FILES;break;
    case FX_S_NATIVE:cap=FX_SDK_NATIVE;break;
    case FX_S_MENU:cap=FX_SDK_MENU;break;
    case FX_S_UI:cap=(q->arg[0]==FX_UI_POLL || q->arg[0]==FX_UI_QUERY || q->arg[0]==FX_UI_FOCUS)?(FX_SDK_QUERY|FX_SDK_MENU):FX_SDK_MENU;break;
    case FX_S_BINDING:case FX_S_PLATFORM:cap=FX_SDK_BINDING;break;
    case FX_S_LIFECYCLE:cap=FX_SDK_QUERY;break;
    case FX_S_SUBSTITUTION:cap=FX_SDK_SUBSTITUTION;break;
    case FX_S_ACCELERATOR:cap=FX_SDK_ACCELERATOR;break;
    default:return FX_CAPABILITY;
    }
    if(!(s->capabilities&cap))return FX_CAPABILITY;
    if(q->operation==FX_S_QUERY) {
        a->value[0]=s->capabilities;a->value[1]=s->owner;a->value[2]=s->time_ms;a->value[3]=s->revision;
        return FX_OK;
    }
    if(q->operation==FX_S_LOG) {
        if(q->bytes>=sizeof(s->last_log))return FX_BOUNDS;
        memset(s->last_log,0,sizeof(s->last_log));memcpy(s->last_log,q->data,q->bytes);return FX_OK;
    }
    if(q->operation==FX_S_PANIC) {fx_service_fault(s);return FX_STATE;}
    if(q->operation==FX_S_EVENT) {
        if(!s->events_count)return FX_EMPTY;
        a->value[0]=s->events[s->events_head][0];a->value[1]=s->events[s->events_head][1];
        s->events_head=(s->events_head+1u)%FX_SERVICE_EVENTS;--s->events_count;return FX_OK;
    }
    if(q->operation==FX_S_REGION_INFO) {
        if(q->arg[0]>=s->region_count)return FX_EMPTY;
        FxServiceRegion *r=&s->regions[q->arg[0]];
        a->value[0]=r->id;a->value[1]=r->bytes;a->value[2]=r->flags;
        memcpy(a->data,r->name,32u);a->bytes=32u;return FX_OK;
    }
    if(q->operation==FX_S_PATCH_ABORT) {
        if(s->patch_committed)return FX_BUSY;
        s->patch_count=0u;return FX_OK;
    }
    if(q->operation==FX_S_PATCH_RESTORE)return restore(s);
    if(q->operation==FX_S_PATCH_COMMIT) {
        if(!s->patch_count || s->patch_committed)return FX_STATE;
        if(q->arg[0]!=s->revision)return FX_STALE;
        for(uint32_t i=0;i<s->patch_count;++i) {
            FxServicePatch *p=&s->patches[i];FxServiceRegion *r=region(s,p->region);
            if(!r || memcmp(r->data+p->offset,p->before,p->bytes))return FX_STALE;
        }
        for(uint32_t i=0;i<s->patch_count;++i) {
            FxServicePatch *p=&s->patches[i];FxServiceRegion *r=region(s,p->region);
            memcpy(r->data+p->offset,p->after,p->bytes);
        }
        s->patch_committed=1u;++s->revision;a->value[0]=s->revision;return FX_OK;
    }
    if(q->operation==FX_S_READ || q->operation==FX_S_WRITE || q->operation==FX_S_PATCH_PREPARE) {
        FxServiceRegion *r=region(s,q->arg[0]);
        if(!r)return FX_CAPABILITY;
        if(q->arg[1]>r->bytes || q->arg[2]>r->bytes-q->arg[1] || !q->arg[2] || q->arg[2]>FX_SERVICE_BYTES)return FX_BOUNDS;
        uint32_t offset=(uint32_t)q->arg[1],n=(uint32_t)q->arg[2];
        if(q->operation==FX_S_READ) {memcpy(a->data,r->data+offset,n);a->bytes=n;return FX_OK;}
        if(!(r->flags&FX_REGION_WRITE))return FX_CAPABILITY;
        if(q->operation==FX_S_WRITE) {
            if(s->patch_count)return FX_BUSY;
            if(q->bytes!=n)return FX_BOUNDS;
            memcpy(r->data+offset,q->data,n);++s->revision;return FX_OK;
        }
        if(!(r->flags&FX_REGION_PATCH))return FX_CAPABILITY;
        if(s->patch_committed || s->patch_count==FX_SERVICE_PATCHES)return FX_BUSY;
        if(n>FX_SERVICE_PATCH_BYTES || q->bytes!=n*2u)return FX_BOUNDS;
        for(uint32_t i=0;i<s->patch_count;++i) {
            FxServicePatch *p=&s->patches[i];
            if(p->region==r->id && offset<p->offset+p->bytes && p->offset<offset+n)return FX_BUSY;
        }
        if(memcmp(r->data+offset,q->data,n))return FX_STALE;
        FxServicePatch *p=&s->patches[s->patch_count++];p->region=r->id;p->offset=offset;p->bytes=n;
        memcpy(p->before,q->data,n);memcpy(p->after,q->data+n,n);a->value[0]=s->revision;return FX_OK;
    }
    return s->backend.call?s->backend.call(s->backend.context,q,a):FX_CAPABILITY;
}
FxResult fx_service_dispatch(FxServiceOwner *s,const FxServiceRequest *request,FxServiceReply *reply) {
    if(!s || !request || !reply)return FX_ARGUMENT;
    FxServiceRequest q;fx_ram_copy(&q,request,sizeof(q));fx_ram_zero(reply,sizeof(*reply));reply->sequence=q.sequence;
    FxResult r;
    if(!s->active)r=FX_STATE;
    else if(q.generation!=s->generation)r=FX_STALE;
    else if(!q.sequence || q.bytes>FX_SERVICE_BYTES)r=FX_BOUNDS;
    else if(q.sequence==s->last_sequence) {
        if(!fx_ram_equal(&q,&s->last_request,sizeof(q)))r=FX_INTEGRITY;
        else {fx_ram_copy(reply,&s->last_reply,sizeof(*reply));return (FxResult)reply->result;}
    } else if(q.sequence!=s->last_sequence+1u)r=FX_STALE;
    else {
        r=execute(s,&q,reply);reply->result=(uint32_t)r;
        s->last_sequence=q.sequence;fx_ram_copy(&s->last_request,&q,sizeof(q));fx_ram_copy(&s->last_reply,reply,sizeof(*reply));return r;
    }
    reply->result=(uint32_t)r;return r;
}
FxResult fx_service_stop(FxServiceOwner *s) {
    if(!s)return FX_ARGUMENT;
    s->active=0u;
    if(s->fault) {fx_service_fault(s);return FX_STATE;}
    if(s->stop_state==FX_SERVICE_STOP_DONE)return FX_OK;
    s->stop_state=FX_SERVICE_STOP_PENDING;
    FxResult stopped=s->backend.stop?s->backend.stop(s->backend.context,0u):FX_OK;
    if(stopped==FX_BUSY)return FX_BUSY;
    if(stopped!=FX_OK || s->fault) {fx_service_fault(s);return stopped==FX_OK?FX_STATE:stopped;}
    FxResult r=s->patch_committed?restore(s):FX_OK;
    if(r==FX_OK) {s->patch_count=0u;s->stop_state=FX_SERVICE_STOP_DONE;}
    return r;
}
void fx_service_fault(FxServiceOwner *s) {
    if(!s)return;
    uint32_t first=!s->fault;s->fault=1u;s->active=0u;
    if(first && s->wake_callback)s->wake_callback(s->wake_context);
    if(s->stop_state!=FX_SERVICE_STOP_FAULT) {
        s->stop_state=FX_SERVICE_STOP_FAULT;
        if(s->backend.stop && s->backend.stop(s->backend.context,1u)==FX_BUSY)
            s->stop_state=FX_SERVICE_STOP_FAULT_PENDING;
    }
}
void fx_service_fault_poll(FxServiceOwner *s) {
    if(s && s->fault && s->stop_state==FX_SERVICE_STOP_FAULT_PENDING)fx_service_fault(s);
}

/* Shared dynamic identity store. An open-addressed table keeps large scans
 * linear on average; growth is transactional, so OOM preserves old records. */
static int index_name(const char *name,char out[16]) {
    if(!name)return 0;
    memset(out,0,16u);uint32_t n=0u;
    while(n<16u && name[n]){out[n]=name[n];++n;}
    return n && n<16u && fx_service_name(out,16u,0);
}
static uint32_t index_hash(const char id[16]) {
    uint32_t hash=2166136261u;
    for(uint32_t i=0;i<16u;++i)hash=(hash^(uint8_t)id[i])*16777619u;
    return hash;
}
void fx_module_index_rehash(FxModuleIndex *s) {
    if(!s || !s->capacity)return;
    memset(s->buckets,0,(size_t)s->capacity*8u);
    for(uint32_t i=0;i<s->count;++i) {
        const char *id=(const char *)s->records+(size_t)i*s->stride;
        uint32_t at=index_hash(id)&(2u*s->capacity-1u);
        while(s->buckets[at])at=(at+1u)&(2u*s->capacity-1u);
        s->buckets[at]=i+1u;
    }
}
FxResult fx_module_index_init(FxModuleIndex *s,FxIndexMemory memory,uint32_t stride,uint32_t budget) {
    if(!s || !memory.allocate || !memory.release || stride<16u || stride>4096u || (stride&3u) || !budget)return FX_ARGUMENT;
    memset(s,0,sizeof(*s));s->memory=memory;s->stride=stride;s->budget=budget;return FX_OK;
}
FxResult fx_module_index_account(FxModuleIndex *s,FxAllocationObserver observer) {
    if(!s)return FX_ARGUMENT;
    if(s->observer.event)return s->observer.event==observer.event && s->observer.context==observer.context?FX_OK:FX_STATE;
    s->observer=observer;
    if(s->records){fx_allocation_observe(observer,s->bytes,FX_ALLOC_REQUESTED);
        if(s->retained)fx_allocation_observe(observer,s->bytes,FX_ALLOC_PUBLISHED);}
    return FX_OK;
}
void fx_module_index_retain(FxModuleIndex *s) {
    if(s && s->records && !s->retained){s->retained=1u;fx_allocation_observe(s->observer,s->bytes,FX_ALLOC_PUBLISHED);}
}
FxResult fx_module_index_reserve(FxModuleIndex *s,uint32_t count) {
    if(!s || !s->memory.allocate || !s->memory.release)return FX_ARGUMENT;
    if(count<=s->capacity)return FX_OK;
    uint32_t capacity=s->capacity?s->capacity:16u;
    while(capacity<count){if(capacity>UINT32_MAX/2u)return FX_BUDGET;capacity*=2u;}
    uint64_t records=(uint64_t)capacity*s->stride,bytes=records+(uint64_t)capacity*8u;
    if(bytes>s->budget || bytes>UINT32_MAX)return FX_BUDGET;
    void *allocation=s->memory.allocate(s->memory.context,(uint32_t)bytes);
    if(!allocation)return FX_BUDGET;
    fx_allocation_observe(s->observer,(uint32_t)bytes,FX_ALLOC_REQUESTED);
    if((uintptr_t)allocation&7u){s->memory.release(s->memory.context,allocation);fx_allocation_observe(s->observer,(uint32_t)bytes,FX_ALLOC_FREE_PENDING);return FX_INTEGRITY;}
    memset(allocation,0,(size_t)bytes);
    if(s->count)memcpy(allocation,s->records,(size_t)s->count*s->stride);
    void *old=s->records;uint32_t old_bytes=s->bytes;
    if(s->retained)fx_allocation_observe(s->observer,(uint32_t)bytes,FX_ALLOC_PUBLISHED);
    s->records=allocation;s->buckets=(uint32_t *)((uint8_t *)allocation+(size_t)records);
    s->capacity=capacity;s->bytes=(uint32_t)bytes;fx_module_index_rehash(s);
    if(old){s->memory.release(s->memory.context,old);fx_allocation_observe(s->observer,old_bytes,s->retained?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);}
    return FX_OK;
}
FxResult fx_module_index_find(const FxModuleIndex *s,const char *name,uint32_t *slot) {
    char id[16];if(!s || !slot || !index_name(name,id))return FX_ARGUMENT;
    if(!s->capacity)return FX_EMPTY;
    uint32_t at=index_hash(id)&(2u*s->capacity-1u);
    for(uint32_t i=0;i<2u*s->capacity;++i) {
        uint32_t entry=s->buckets[at];if(!entry)return FX_EMPTY;
        if(entry>s->count)return FX_INTEGRITY;
        if(!memcmp((const uint8_t *)s->records+(size_t)(entry-1u)*s->stride,id,16u)){*slot=entry-1u;return FX_OK;}
        at=(at+1u)&(2u*s->capacity-1u);
    }
    return FX_INTEGRITY;
}
FxResult fx_module_index_add(FxModuleIndex *s,const char *name,uint32_t *slot) {
    char id[16];if(!s || !slot || !index_name(name,id))return FX_ARGUMENT;
    FxResult r=fx_module_index_find(s,id,slot);
    if(r==FX_OK)return FX_INTEGRITY;
    if(r!=FX_EMPTY)return r;
    if(s->count==UINT32_MAX)return FX_BUDGET;
    r=fx_module_index_reserve(s,s->count+1u);if(r!=FX_OK)return r;
    uint32_t at=index_hash(id)&(2u*s->capacity-1u);
    while(s->buckets[at])at=(at+1u)&(2u*s->capacity-1u);
    uint8_t *record=(uint8_t *)s->records+(size_t)s->count*s->stride;
    memset(record,0,s->stride);memcpy(record,id,16u);*slot=s->count++;
    s->buckets[at]=*slot+1u;return FX_OK;
}
void fx_module_index_destroy(FxModuleIndex *s) {
    if(!s)return;
    if(s->records){s->memory.release(s->memory.context,s->records);fx_allocation_observe(s->observer,s->bytes,s->retained?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);}
    s->records=0;s->buckets=0;s->count=s->capacity=s->bytes=s->retained=0u;
}

FxResult fx_module_index_shrink(FxModuleIndex *s) {
    if(!s)return FX_ARGUMENT;
    if(!s->count){fx_module_index_destroy(s);return FX_OK;}
    if(s->capacity<=16u || s->count>s->capacity/4u)return FX_OK;
    FxModuleIndex next;FxResult r=fx_module_index_init(&next,s->memory,s->stride,s->budget);
    if(r!=FX_OK)return r;
    (void)fx_module_index_account(&next,s->observer);
    r=fx_module_index_reserve(&next,s->count);if(r!=FX_OK)return r;
    memcpy(next.records,s->records,(size_t)s->count*s->stride);next.count=s->count;
    fx_module_index_rehash(&next);if(s->retained)fx_module_index_retain(&next);
    FxModuleIndex old=*s;*s=next;fx_module_index_destroy(&old);return FX_OK;
}
