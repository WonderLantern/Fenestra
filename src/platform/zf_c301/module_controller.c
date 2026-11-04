#include "owner_storage.h"
#include "fenestra_memory.h"
enum {EMPTY,LOADING,WAITING,ACTIVE,STOPPED};
const char *fx_zf_modules_fault_stage(const FxZfModuleController *c) {
    if(!c)return "Fenestra";
    switch(c->fault_stage) {
    case FX_ZF_LOAD_DISCOVERY:return "Fenestra: discovery";
    case FX_ZF_LOAD_DIRECTORY_OPEN:return "Fenestra: dir open";
    case FX_ZF_LOAD_DIRECTORY_NEXT:return "Fenestra: dir next";
    case FX_ZF_LOAD_DIRECTORY_ENTRY:return "Fenestra: dir entry";
    case FX_ZF_LOAD_PACKAGE_READ:return "Fenestra: module read";
    case FX_ZF_LOAD_PACKAGE_LENGTH:return "Fenestra: file length";
    case FX_ZF_LOAD_PACKAGE_CHECK:
        return c->error==FX_CAPABILITY && c->check_detail[0]?c->check_detail:"Fenestra: module check";
    case FX_ZF_LOAD_DIRECTORY_CLOSE:return "Fenestra: dir close";
    case FX_ZF_LOAD_WORKER_SLOTS:return "Fenestra: worker slots";
    case FX_ZF_LOAD_LAYOUT:return "Fenestra: arena layout";
    case FX_ZF_LOAD_ALLOCATE:return "Fenestra: allocation";
    case FX_ZF_LOAD_SERVICES:return "Fenestra: services";
    case FX_ZF_LOAD_WORKER_START:return "Fenestra: worker start";
    case FX_ZF_LOAD_WORKER_WAIT:return "Fenestra: worker wait";
    case FX_ZF_LOAD_WORKER_PANIC:return "Fenestra: worker panic";
    default:return "Fenestra";
    }
}
/* Observe existing IO without changing data, status or ownership. A successful
 * close must not erase an earlier read/verification failure boundary. */
static FxResult directory_open(void *context) {
    FxZfModuleController *c=context;c->stage=FX_ZF_LOAD_DIRECTORY_OPEN;
    return c->source_directory.open(c->source_directory.context);
}
static FxResult directory_next(void *context,FxModuleDirectoryEntry *out) {
    FxZfModuleController *c=context;c->stage=FX_ZF_LOAD_DIRECTORY_NEXT;
    FxResult r=c->source_directory.next(c->source_directory.context,out);
    if(r==FX_OK){c->stage=FX_ZF_LOAD_DIRECTORY_ENTRY;c->directory_bytes=out->bytes;}
    return r;
}
static FxResult directory_close(void *context) {
    FxZfModuleController *c=context;
    FxResult r=c->source_directory.close(c->source_directory.context);
    if(r!=FX_OK)c->stage=FX_ZF_LOAD_DIRECTORY_CLOSE;
    return r;
}
static uint32_t header_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void check_detail(FxZfModuleController *c,const char *field,uint32_t value,uint32_t expected) {
    char *out=c->check_detail;
    const char *prefix="Fenestra: ";
    while(*prefix)*out++=*prefix++;
    while(*field)*out++=*field++;
    *out++=' ';
    for(uint32_t n=8u;n;--n)*out++="0123456789ABCDEF"[(value>>((n-1u)*4u))&15u];
    *out++='/';
    for(uint32_t n=8u;n;--n)*out++="0123456789ABCDEF"[(expected>>((n-1u)*4u))&15u];
    *out=0;
}
/* Observe exactly the original admission predicates, without returning a new
 * status or modifying the package. Only an original FX_CAPABILITY failure
 * displays this copied detail. Values are public .wlm header fields. */
static void inspect_header(FxZfModuleController *c,const uint8_t *data,uint32_t bytes) {
    c->check_detail[0]=0;
    if(bytes<FX_MODULE_HEADER)return;
    uint32_t version=header_u32(data+8u);
    if(version<1u || version>3u){check_detail(c,"format",version,2u);return;}
    if(version<3u)for(uint32_t i=version==2u?120u:116u;i<FX_MODULE_HEADER;++i)
        if(data[i]){check_detail(c,"reserved",i,data[i]);return;}
    if(fx_module_header_entry(data,bytes)!=FX_MODULE_ENTRY_SERVICES) {
        check_detail(c,"entry",version>=2u?header_u32(data+116u):0u,FX_MODULE_ENTRY_SERVICES);return;
    }
    uint32_t abi=header_u32(data+16u),isa=header_u32(data+20u);
    if(abi!=FX_ABI){check_detail(c,"ABI",abi,FX_ABI);return;}
    if(isa!=FX_ISA_AARCH64){check_detail(c,"ISA",isa,FX_ISA_AARCH64);return;}
    const FxPlatform *platform=fx_platform_target();
    if(!platform)return;
    for(uint32_t i=0u;i<32u;++i)if(data[24u+i]!=platform->identity[i]) {
        char field[]="target+00";
        field[7]="0123456789ABCDEF"[i>>4];field[8]="0123456789ABCDEF"[i&15u];
        check_detail(c,field,data[24u+i],platform->identity[i]);return;
    }
    uint32_t low=header_u32(data+108u),high=header_u32(data+112u);
    if(high){check_detail(c,"caps high",high,0u);return;}
    if(low&~FX_SDK_CAPS){check_detail(c,"caps",low,FX_SDK_CAPS);return;}
    check_detail(c,"header passed",0u,0u);
}
static FxResult package_read(void *context,const char *name,uint8_t *data,uint32_t capacity,uint32_t *bytes) {
    FxZfModuleController *c=context;c->stage=FX_ZF_LOAD_PACKAGE_READ;
    FxResult r=c->source_directory.read(c->source_directory.context,name,data,capacity,bytes);
    c->read_bytes=*bytes;
    if(r==FX_OK)c->stage=*bytes==c->directory_bytes?FX_ZF_LOAD_PACKAGE_CHECK:FX_ZF_LOAD_PACKAGE_LENGTH;
    if(r==FX_OK && *bytes<=capacity && *bytes==c->directory_bytes)inspect_header(c,data,*bytes);
    return r;
}
static const FxNamedBinding *binding(const char *name) {
    const FxPlatform *platform=fx_platform_target();
    for(uint32_t i=0u;platform && i<platform->count;++i) {
        const char *a=platform->bindings[i].name,*b=name;
        if(!a)continue;
        while(*a && *a==*b){++a;++b;}
        if(!*a && !*b)return &platform->bindings[i];
    }
    return 0;
}
static void *allocate(void *context,uint32_t bytes) {
    (void)context;
    const FxNamedBinding *b=binding("rtos.memory.allocate");
    return b?((void *(*)(uint32_t,uint32_t))(uintptr_t)b->address)(bytes,0u):0;
}
static void release(void *context,void *memory) {
    (void)context;
    const FxNamedBinding *b=binding("rtos.memory.release");
    if(b)((void (*)(void *))(uintptr_t)b->address)(memory);
}
static FxResult fault(FxZfOwnerStorage *owner,FxResult result) {
    FxZfModuleController *c=&owner->module_controller;
    if(!c->error){c->error=result;c->fault_stage=c->stage;}
    fx_zf_owner_fault(owner,0,result);return result;
}
FxResult fx_zf_modules_step(FxZfOwnerStorage *owner,uint32_t stop) {
    if(!owner)return FX_ARGUMENT;
    FxZfModuleController *c=&owner->module_controller;
    FxNativeModules *manager=owner->modules_manager;
    uint32_t count=atomic_load_explicit(&c->count,memory_order_acquire);
    if(stop) {
        if(manager)fx_module_manager_shutdown(&manager->manager);
        if(c->inventory.entries)fx_modules_destroy(&c->inventory);
        uint32_t pending=0u;
        for(uint32_t i=0u;i<count;++i)if(c->workers[i]) {
            fx_zf_worker_shutdown(c->workers[i]);
            uint32_t phase=fx_zf_worker_phase(c->workers[i]);
            if(phase!=FX_ZF_WORKER_QUIESCED && phase!=FX_ZF_WORKER_PANIC)pending=1u;
        }
        if(pending)return FX_BUSY;
        c->phase=STOPPED;return FX_OK;
    }
    if(c->phase==STOPPED)return FX_STATE;
    for(uint32_t i=0u;i<count;++i)if(c->workers[i] &&
        fx_zf_worker_phase(c->workers[i])==FX_ZF_WORKER_PANIC) {
        FxResult result=(FxResult)fx_zf_worker_error(c->workers[i]);
        c->stage=FX_ZF_LOAD_WORKER_PANIC;
        fx_zf_owner_fault(owner,&c->services[i]->platform,result);
        return fault(owner,result);
    }
    if(c->error)return c->error;
    if(c->phase==EMPTY) {
        if(!manager || !manager->manager.activated)return FX_STATE;
        /* Original Z9 activation re-discovers enabled packages, then starts
         * one independent worker/session at a time until source scratch is
         * released. The frozen manager blocks rename/scan during this pass. */
        const FxPlatform *platform=fx_platform_target();
        c->stage=FX_ZF_LOAD_DISCOVERY;c->source_directory=manager->manager.io.directory;
        const FxModuleDirectory directory={c,directory_open,directory_next,directory_close,package_read};
        FxResult r=fx_modules_discover_accounted(&directory,platform->identity,
            FX_SDK_CAPS,c->scratch,sizeof(c->scratch),&c->inventory,
            (FxIndexMemory){0,allocate,release},fx_account_observer(&owner->accounting.indexes),
            fx_account_observer(&owner->accounting.inspect));
        if(r!=FX_OK && r!=FX_EMPTY)return fault(owner,r);
        c->stage=FX_ZF_LOAD_WORKER_SLOTS;
        c->capacity=c->inventory.count;
        if(c->capacity>FX_MODULE_OWNER_LIMIT)return fault(owner,FX_BUDGET);
        if(c->capacity) {
            uint32_t bytes=c->capacity*2u*(uint32_t)sizeof(uintptr_t);
            c->workers=allocate(0,bytes);
            if(!c->workers)return fault(owner,FX_BUDGET);
            memset(c->workers,0,bytes);c->services=(FxZfServices **)(c->workers+c->capacity);
            (void)fx_account_acquire(&owner->accounting.loader,bytes,1u,1u);
        }
        c->phase=LOADING;
    }
    if(c->phase==WAITING) {
        c->stage=FX_ZF_LOAD_WORKER_WAIT;
        uint32_t phase=fx_zf_worker_phase(c->workers[c->next]);
        if(phase==FX_ZF_WORKER_LOADING)return FX_BUSY;
        if(phase!=FX_ZF_WORKER_ACTIVE && phase!=FX_ZF_WORKER_FINISHED)return fault(owner,FX_STATE);
        ++c->next;c->phase=LOADING;
    }
    if(c->phase==LOADING) {
        while(c->next<c->capacity && !c->inventory.entries[c->next].enabled)++c->next;
        if(c->next==c->capacity) {
            fx_modules_destroy(&c->inventory);c->phase=ACTIVE;
            fx_module_manager_ready(&manager->manager);return FX_OK;
        }
        const FxDiscoveredModule *entry=&c->inventory.entries[c->next];
        c->stage=FX_ZF_LOAD_LAYOUT;
        FxExecLayout layout;
        FxResult r=fx_zf_session_layout_stacks(entry->bytes,entry->image_bytes,entry->stacks,&layout);
        if(r!=FX_OK)return fault(owner,r);
        uint32_t bytes=fx_zf_worker_bytes(),session_bytes=layout.bytes+4096u;
        c->stage=FX_ZF_LOAD_ALLOCATE;
        void *worker=allocate(0,bytes),*session=allocate(0,session_bytes);
        FxZfServices *services=allocate(0,(uint32_t)sizeof(*services));
        if(!worker || !session || !services) {
            if(worker)release(0,worker);
            if(session)release(0,session);
            if(services)release(0,services);
            return fault(owner,FX_BUDGET);
        }
        memset(worker,0,bytes);memset(session,0,session_bytes);memset(services,0,sizeof(*services));
        uint32_t caps=(entry->required|entry->optional)&FX_SDK_CAPS;
        c->services[c->next]=services;
        c->stage=FX_ZF_LOAD_SERVICES;
        r=fx_zf_services_init_worker(services,c->next+1u,1u,caps);
        if(r==FX_OK)(void)fx_account_acquire(&services->accounting.allocation,
            (uint64_t)bytes+session_bytes+sizeof(*services),3u,1u);
        if(r==FX_OK) {
            c->stage=FX_ZF_LOAD_WORKER_START;
            r=fx_zf_worker_start(worker,bytes,session,session_bytes,entry,c->next+1u,1u,caps,
                (FxServiceBackend){services,fx_zf_service_call,fx_zf_service_stop},&c->workers[c->next]);
        }
        if(c->workers[c->next])atomic_store_explicit(&c->count,c->next+1u,memory_order_release);
        if(r!=FX_OK)return fault(owner,r);
        c->phase=WAITING;return FX_BUSY;
    }
    return FX_OK;
}
FxResult fx_zf_modules_action(FxZfModuleController *c,FxPlatformOwner *owner,uint32_t action) {
    if(!c || !owner || !action)return FX_ARGUMENT;
    for(uint32_t i=0u;i<atomic_load_explicit(&c->count,memory_order_acquire);++i)
        if(c->services[i] && &c->services[i]->platform==owner)
            return fx_zf_worker_action(c->workers[i],action);
    return FX_EMPTY;
}
