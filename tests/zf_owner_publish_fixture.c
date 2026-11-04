#define _XOPEN_SOURCE 700
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "owner_publish.h"
#include "fenestra_platform.h"
static const FxPlatform target={.abi=FX_PLATFORM_ABI};
const FxPlatform *fx_platform_target(void){return &target;}
FxPtp *fx_zf_ptp_owner(void){return 0;}
void fx_linux_revoke(void){}
FxResult fx_zf_ptp_bind(const FxPtpIdentity *identity){(void)identity;return FX_STATE;}
const FxNativePtpDescriptor *fx_zf_ptp_original_lookup(uint32_t op){(void)op;return 0;}
void fx_zf_ptp_original_session(uint32_t session){(void)session;}
uint32_t fx_zf_ptp_original_operations(uint8_t *out,uint32_t cap,const uint8_t *ops){
    (void)out;(void)cap;(void)ops;return 0u;
}
FxZfLifecycle *fx_zf_runtime_lifecycle(void){return 0;}
typedef struct {
    FxNativePage page;FxNativeDescriptor descriptor;
    pthread_barrier_t barrier;_Atomic unsigned allocated,released;
} Fixture;
static void *allocate(void *context,uint32_t bytes) {
    Fixture *f=context;void *p=malloc(bytes);assert(p);
    atomic_fetch_add(&f->allocated,1u);
    int r=pthread_barrier_wait(&f->barrier);assert(r==0 || r==PTHREAD_BARRIER_SERIAL_THREAD);
    return p;
}
static void release(void *context,void *memory) {
    Fixture *f=context;
    assert(atomic_load((_Atomic uintptr_t *)&f->page.descriptor)!=(uintptr_t)memory);
    atomic_fetch_add(&f->released,1u);free(memory);
}
typedef struct {Fixture *f;FxResult result;} Worker;
static void *publish(void *context) {
    Worker *w=context;FxZfOwnerMemory memory={w->f,allocate,release};
    w->result=fx_zf_owner_publish(&w->f->page,&w->f->descriptor,&memory);return 0;
}
int main(void) {
    for(unsigned n=0;n<100;n++) {
        Fixture f={0};f.descriptor.flags=0x80001002u;f.descriptor.count=47u;
        f.descriptor.header=0x43b38c80u;f.descriptor.rows=0x43b38d90u;f.descriptor.events=0x43b395a8u;
        for(unsigned i=0;i<15;i++)f.descriptor.geometry[i]=(uint16_t)(i+100u);
        f.page.identity=0x02a00002u;f.page.descriptor=(uintptr_t)&f.descriptor;
        FxNativeDescriptor original=f.descriptor;
        assert(!pthread_barrier_init(&f.barrier,0,2));
        Worker workers[]={{&f,FX_EMPTY},{&f,FX_EMPTY}};pthread_t threads[2];
        for(unsigned i=0;i<2;i++)assert(!pthread_create(&threads[i],0,publish,&workers[i]));
        for(unsigned i=0;i<2;i++)assert(!pthread_join(threads[i],0));
        assert((workers[0].result==FX_OK)+(workers[1].result==FX_OK)==1);
        assert(atomic_load(&f.allocated)==2u && atomic_load(&f.released)==1u);
        FxZfOwnerStorage *owner=(void *)atomic_load((_Atomic uintptr_t *)&f.page.descriptor);
        assert(!memcmp(&owner->stock,&original,sizeof(original)));
        assert(!memcmp(&f.descriptor,&original,sizeof(original)));
        assert(owner->page==(uintptr_t)&f.page && atomic_load(&owner->lifecycle.word)==0x101u);
        assert(atomic_load(&owner->ptp.generation)==1u && !atomic_load(&owner->ptp.enabled));
        assert(owner->ptp.identity==&owner->ptp_identity && owner->ptp_identity.firmware==301u);
        fx_zf_lifecycle_record(&owner->lifecycle,8u);
        const FxZfOwnerMemory memory={&f,allocate,release};
        assert(fx_zf_owner_publish(&f.page,&f.descriptor,&memory)==FX_STALE);
        assert(!fx_zf_lifecycle_accepts_work(&owner->lifecycle));
        assert(atomic_load(&f.allocated)==2u && atomic_load(&f.released)==1u);
        assert(!pthread_barrier_destroy(&f.barrier));
        /* Fixture process owns all readers, joined above. Production retains
         * this allocation until reboot; this is host fixture teardown only. */
        free(owner);
    }
    return 0;
}
