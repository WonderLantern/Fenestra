#include "owner_storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
struct FxZfModuleWorker {uint32_t phase,error,stop,owner;FxDiscoveredModule entry;};
static FxZfOwnerStorage owner;
static FxNativeModules manager;
static FxDiscoveredModule entries[3];
static uint32_t allocations,fail_allocation,releases,discoveries,destroys,starts,ready,faults;
static uint32_t fault_owner,start_failure,service_failure,discovery_failure,short_read,close_failure;
static uint32_t discovery_result,header_fault;
static void *native_allocate(uint32_t bytes,uint32_t pool) {
    assert(!pool);++allocations;
    return allocations==fail_allocation?0:calloc(1,bytes);
}
static void native_release(void *p){assert(p);++releases;free(p);}
static FxNamedBinding bindings[2];
static FxPlatform platform;
const FxPlatform *fx_platform_target(void){return &platform;}
FxResult fx_account_acquire(FxAccountRow *r,uint64_t b,uint64_t u,uint32_t retained) {
    (void)r;assert(b && u && retained);return FX_OK;
}
FxAllocationObserver fx_account_observer(FxAccountRow *r){(void)r;return (FxAllocationObserver){0};}
FxResult fx_modules_discover_accounted(const FxModuleDirectory *io,const uint8_t fw[32],
    uint32_t caps,uint8_t *scratch,uint32_t size,FxModuleInventory *out,FxIndexMemory memory,
    FxAllocationObserver indexes,FxAllocationObserver packages) {
    (void)io;(void)fw;(void)caps;(void)indexes;(void)packages;
    assert(scratch && size>=16385u && memory.allocate && memory.release);
    ++discoveries;
    if(discovery_failure) {
        assert(io->open(io->context)==FX_OK);
        FxModuleDirectoryEntry item={0};assert(io->next(io->context,&item)==FX_OK);
        uint32_t bytes=0;assert(io->read(io->context,item.name,scratch,size,&bytes)==FX_OK);
        FxResult closed=io->close(io->context);
        return closed!=FX_OK?closed:discovery_result?(FxResult)discovery_result:FX_INTEGRITY;
    }
    out->entries=entries;out->count=3;return FX_OK;
}
void fx_modules_destroy(FxModuleInventory *i){++destroys;i->entries=0;i->count=0;}
void fx_module_manager_ready(FxModuleManager *m){assert(m==&manager.manager);++ready;}
void fx_module_manager_shutdown(FxModuleManager *m){atomic_store(&m->shutdown,1);}
void fx_zf_owner_fault(FxZfOwnerStorage *o,const FxPlatformOwner *p,FxResult r) {
    assert(o==&owner && r!=FX_OK);++faults;if(!fault_owner && p)fault_owner=p->owner;
}
FxResult fx_zf_session_layout_stacks(uint32_t package,uint32_t image,FxModuleStacks stacks,FxExecLayout *out) {
    assert(package==200 && image==4096 && stacks.native_bytes==65536);out->bytes=8192;return FX_OK;
}
uint32_t fx_zf_worker_bytes(void){return sizeof(FxZfModuleWorker);}
FxResult fx_zf_services_init_worker(FxZfServices *s,uint32_t id,uint32_t generation,uint32_t caps) {
    assert(generation==1 && !caps);s->platform.owner=id;return service_failure?FX_IO:FX_OK;
}
FxResult fx_zf_service_call(void *p,const FxServiceRequest *q,FxServiceReply *r){(void)p;(void)q;(void)r;return FX_OK;}
FxResult fx_zf_service_stop(void *p,uint32_t panic){(void)p;(void)panic;return FX_OK;}
FxResult fx_zf_worker_start(void *w,uint32_t bytes,void *session,uint32_t session_bytes,
    const FxDiscoveredModule *e,uint32_t id,uint32_t generation,uint32_t caps,FxServiceBackend backend,
    FxZfModuleWorker **out) {
    assert(bytes==sizeof(FxZfModuleWorker) && session && session_bytes==12288);
    assert(e->enabled && generation==1 && !caps && backend.call && backend.stop);
    FxZfModuleWorker *worker=w;worker->entry=*e;worker->owner=id;
    worker->phase=start_failure?FX_ZF_WORKER_PANIC:FX_ZF_WORKER_LOADING;
    worker->error=start_failure?FX_IO:0;*out=worker;++starts;
    return start_failure?FX_IO:FX_OK;
}
void fx_zf_worker_shutdown(FxZfModuleWorker *w){++w->stop;}
uint32_t fx_zf_worker_phase(const FxZfModuleWorker *w){return w->phase;}
uint32_t fx_zf_worker_error(const FxZfModuleWorker *w){return w->error;}
FxResult fx_zf_worker_action(FxZfModuleWorker *w,uint32_t action){assert(action==7);return w->owner==3?FX_OK:FX_STATE;}
static FxResult dir_open(void *p){assert(p==&manager);return FX_OK;}
static FxResult dir_next(void *p,FxModuleDirectoryEntry *out) {
    assert(p==&manager);strcpy(out->name,"demo-zf.wlm");out->bytes=7647u;return FX_OK;
}
static FxResult dir_close(void *p){assert(p==&manager);return close_failure?FX_CLOSE:FX_OK;}
static FxResult dir_read(void *p,const char *name,uint8_t *data,uint32_t capacity,uint32_t *bytes) {
    assert(p==&manager && !strcmp(name,"demo-zf.wlm") && data && capacity>=7648u);
    memset(data,0,7648u);memcpy(data,"FENESTRA",8u);
    data[8]=2u;data[12]=128u;data[16]=1u;data[20]=1u;data[110]=0x3fu;data[116]=1u;
    if(header_fault==1u)data[8]=0u;
    if(header_fault==2u)data[120]=7u;
    if(header_fault==3u)data[116]=0u;
    if(header_fault==4u)data[16]=2u;
    if(header_fault==5u)data[20]=2u;
    if(header_fault==6u)data[31]=7u;
    if(header_fault==7u)data[112]=1u;
    if(header_fault==8u)data[110]=0x80u;
    *bytes=short_read?7646u:7647u;return FX_OK;
}
static void reset(void) {
    memset(&owner,0,sizeof(owner));memset(&manager,0,sizeof(manager));memset(entries,0,sizeof(entries));
    owner.modules_manager=&manager;
    allocations=fail_allocation=releases=discoveries=destroys=starts=ready=faults=fault_owner=0;
    start_failure=service_failure=discovery_failure=short_read=close_failure=0;
    discovery_result=header_fault=0;
    manager.manager.io.directory=(FxModuleDirectory){&manager,dir_open,dir_next,dir_close,dir_read};
    for(uint32_t i=0;i<3;++i){entries[i].bytes=200;entries[i].image_bytes=4096;
        entries[i].stacks=(FxModuleStacks){65536,65536};entries[i].enabled=i!=1;}
    strcpy(entries[0].file,"one-zf.wlm");strcpy(entries[2].file,"two-zf.wlm");
    bindings[0]=(FxNamedBinding){.name="rtos.memory.allocate",.address=(uintptr_t)native_allocate};
    bindings[1]=(FxNamedBinding){.name="rtos.memory.release",.address=(uintptr_t)native_release};
    platform=(FxPlatform){.count=2,.bindings=bindings};
}
static void check_capability_header(uint32_t mutation,const char *expected) {
    reset();manager.manager.activated=1;discovery_failure=1;
    discovery_result=FX_CAPABILITY;header_fault=mutation;
    assert(fx_zf_modules_step(&owner,0)==FX_CAPABILITY && !starts);
    assert(owner.module_controller.fault_stage==FX_ZF_LOAD_PACKAGE_CHECK);
    assert(!strcmp(fx_zf_modules_fault_stage(&owner.module_controller),expected));
    assert(fx_zf_modules_step(&owner,1)==FX_OK);
    assert(!strcmp(fx_zf_modules_fault_stage(&owner.module_controller),expected));
}
int main(void) {
    reset();assert(fx_zf_modules_step(0,0)==FX_ARGUMENT);
    assert(fx_zf_modules_step(&owner,0)==FX_STATE && !discoveries && !allocations);
    manager.manager.activated=1;
    assert(fx_zf_modules_step(&owner,0)==FX_BUSY && starts==1 && discoveries==1);
    FxZfModuleController *c=&owner.module_controller;
    assert(atomic_load(&c->count)==1 && !ready && !destroys);
    assert(fx_zf_modules_step(&owner,0)==FX_BUSY && starts==1);
    c->workers[0]->phase=FX_ZF_WORKER_ACTIVE;
    assert(fx_zf_modules_step(&owner,0)==FX_BUSY && starts==2 && !c->workers[1]);
    assert(c->workers[2]->owner==3 && !strcmp(c->workers[2]->entry.file,"two-zf.wlm"));
    c->workers[2]->phase=FX_ZF_WORKER_FINISHED;
    assert(fx_zf_modules_step(&owner,0)==FX_OK && destroys==1 && ready==1 && !faults);
    assert(atomic_load(&c->count)==3 && !c->inventory.entries);
    assert(fx_zf_modules_action(c,&c->services[2]->platform,7)==FX_OK);
    FxPlatformOwner foreign={0};assert(fx_zf_modules_action(c,&foreign,7)==FX_EMPTY);
    assert(fx_zf_modules_step(&owner,1)==FX_BUSY && c->workers[0]->stop && c->workers[2]->stop);
    c->workers[0]->phase=c->workers[2]->phase=FX_ZF_WORKER_QUIESCED;
    assert(fx_zf_modules_step(&owner,1)==FX_OK && atomic_load(&manager.manager.shutdown));
    assert(fx_zf_modules_step(&owner,0)==FX_STATE && starts==2);
    reset();manager.manager.activated=1;fail_allocation=3;
    assert(fx_zf_modules_step(&owner,0)==FX_BUDGET && !starts && releases==2 && faults);
    assert(fx_zf_modules_step(&owner,1)==FX_OK && destroys==1);
    reset();manager.manager.activated=1;start_failure=1;
    assert(fx_zf_modules_step(&owner,0)==FX_IO && starts==1 && !releases);
    assert(owner.module_controller.fault_stage==FX_ZF_LOAD_WORKER_START);
    assert(atomic_load(&owner.module_controller.count)==1);
    assert(fx_zf_modules_step(&owner,1)==FX_OK && destroys==1);
    reset();manager.manager.activated=1;
    assert(fx_zf_modules_step(&owner,0)==FX_BUSY);
    owner.module_controller.workers[0]->phase=FX_ZF_WORKER_PANIC;
    owner.module_controller.workers[0]->error=FX_TIMEOUT;
    assert(fx_zf_modules_step(&owner,0)==FX_TIMEOUT && fault_owner==1);
    assert(fx_zf_modules_step(&owner,1)==FX_OK && destroys==1);
    reset();manager.manager.activated=1;service_failure=1;
    assert(fx_zf_modules_step(&owner,0)==FX_IO && !starts && !releases);
    assert(owner.module_controller.fault_stage==FX_ZF_LOAD_SERVICES);
    assert(fx_zf_modules_step(&owner,1)==FX_OK && destroys==1);
    reset();manager.manager.activated=1;entries[0].enabled=entries[2].enabled=0;
    assert(fx_zf_modules_step(&owner,0)==FX_OK && !starts && ready==1 && destroys==1);
    reset();manager.manager.activated=1;discovery_failure=short_read=1;
    assert(fx_zf_modules_step(&owner,0)==FX_INTEGRITY && !starts);
    assert(owner.module_controller.fault_stage==FX_ZF_LOAD_PACKAGE_LENGTH);
    assert(!strcmp(fx_zf_modules_fault_stage(&owner.module_controller),"Fenestra: file length"));
    assert(owner.module_controller.directory_bytes==7647u && owner.module_controller.read_bytes==7646u);
    assert(fx_zf_modules_step(&owner,1)==FX_OK);
    assert(owner.module_controller.fault_stage==FX_ZF_LOAD_PACKAGE_LENGTH);
    reset();manager.manager.activated=1;discovery_failure=1;
    assert(fx_zf_modules_step(&owner,0)==FX_INTEGRITY && !starts);
    assert(owner.module_controller.fault_stage==FX_ZF_LOAD_PACKAGE_CHECK);
    reset();manager.manager.activated=1;discovery_failure=close_failure=1;
    assert(fx_zf_modules_step(&owner,0)==FX_CLOSE && !starts);
    assert(owner.module_controller.fault_stage==FX_ZF_LOAD_DIRECTORY_CLOSE);
    assert(!strcmp(fx_zf_modules_fault_stage(0),"Fenestra"));
    check_capability_header(1u,"Fenestra: format 00000000/00000002");
    check_capability_header(2u,"Fenestra: reserved 00000078/00000007");
    check_capability_header(3u,"Fenestra: entry 00000000/00000001");
    check_capability_header(4u,"Fenestra: ABI 00000002/00000001");
    check_capability_header(5u,"Fenestra: ISA 00000002/00000001");
    check_capability_header(6u,"Fenestra: target+07 00000007/00000000");
    check_capability_header(7u,"Fenestra: caps high 00000001/00000000");
    check_capability_header(8u,"Fenestra: caps 00800000/007F0000");
    check_capability_header(0u,"Fenestra: header passed 00000000/00000000");
    return 0;
}
