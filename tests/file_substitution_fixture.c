#include "fenestra_overlay_file.h"
#include "fenestra_extension_services.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t original[]="mode=original\n";
static const uint8_t replacement[]="mode=card\n";

typedef struct {
    uint32_t opens,closes,frees,position;
    int missing,fail_close;
} Fixture;

static FxResult reserve(void *context,uint32_t size,FxOverlayAllocation *allocation) {
    (void)context;
    uint8_t *data=malloc(size);
    if(!data)return FX_BUDGET;
    *allocation=(FxOverlayAllocation){data,size,(uint64_t)(uintptr_t)data};
    return FX_OK;
}

static FxResult release(void *context,uint64_t token) {
    Fixture *fixture=context;
    ++fixture->frees;
    free((void *)(uintptr_t)token);
    return FX_OK;
}

static FxResult policy(void *context,uint32_t target,FxOverlayActor actor,
                      uint64_t lease,const uint8_t *data,uint32_t bytes,const uint8_t schema[32]) {
    (void)context;
    (void)schema;
    return target==1u && actor.owner==2u && lease==42u && data && bytes?FX_OK:FX_CAPABILITY;
}

static int32_t open_file(void *context,const char *path,uint32_t mode) {
    Fixture *fixture=context;
    assert(mode==FX_TEXT_READ && !strcmp(path,"A:/FENESTRA/OVERLAY/build.prop"));
    ++fixture->opens;
    fixture->position=0u;
    return fixture->missing?-4:0;
}

static uint32_t read_file(void *context,int32_t fd,void *data,uint32_t max,uint32_t *actual) {
    Fixture *fixture=context;
    assert(fd==0);
    *actual=(uint32_t)sizeof(replacement)-fixture->position;
    if(*actual>max)*actual=max;
    if(*actual>3u)*actual=3u;
    memcpy(data,replacement+fixture->position,*actual);
    fixture->position+=*actual;
    return 0u;
}

static uint32_t close_file(void *context,int32_t fd) {
    Fixture *fixture=context;
    assert(fd==0);
    ++fixture->closes;
    return (uint32_t)fixture->fail_close;
}

static FxResult load(FxOverlayCardLoad *job) {
    for(uint32_t i=0u;i<sizeof(replacement)+1u;++i) {
        const FxResult result=fx_overlay_bulk_step(&job->bulk);
        if(result!=FX_BUSY)return result;
    }
    assert(0);
    return FX_TIMEOUT;
}

int main(void) {
    Fixture fixture={0};
    FxOverlay store;
    FxOverlayCardLoad job={0};
    const FxOverlayActor publisher={2u,3u},consumer={4u,3u};
    const char binding[]="/system/build.prop";
    FxOverlayTargetSpec spec={.id=1u,.publisher=2u,.original=original,
        .original_bytes=sizeof(original),.max_bytes=64u,.binding="/system/build.prop"};
    fx_sha256(original,sizeof(original),spec.original_sha256);
    const FxOverlayMemory memory={&fixture,reserve,release};
    const FxOverlayPolicy leases={0,policy};
    const FxOverlayCardSource source={
        .files={.context=&fixture,.open=open_file,.read=read_file,.close=close_file},
        .directory="A:/FENESTRA/OVERLAY/",.missing_error=(uint32_t)-4
    };
    FxOverlayCardImage image={.target=1u,.revision=1u,.bytes=sizeof(replacement)};
    memcpy(image.original_sha256,spec.original_sha256,32u);
    fx_sha256(replacement,sizeof(replacement),image.replacement_sha256);

    uint64_t handle=0u;
    uint32_t bytes=0u;
    fx_overlay_init(&store,3u,256u,memory,leases);
    assert(fx_overlay_register(&store,&spec)==FX_OK);
    assert(fx_overlay_card_begin(&job,&store,publisher,&source,"build.prop",&image)==FX_STATE);
    assert(!fixture.opens);
    assert(fx_overlay_file_open(&store,consumer,binding,&handle,&bytes)==FX_EMPTY);
    assert(fx_overlay_activate(&store)==FX_OK);
    assert(fx_overlay_card_begin(&job,&store,publisher,&source,"../build.prop",&image)==FX_ARGUMENT);
    assert(!fixture.opens);
    fixture.missing=1;
    assert(fx_overlay_card_begin(&job,&store,publisher,&source,"build.prop",&image)==FX_EMPTY);
    assert(!fixture.closes && !store.retained);
    fixture.missing=0;

    assert(fx_overlay_card_begin(&job,&store,publisher,&source,"build.prop",&image)==FX_OK);
    assert(load(&job)==FX_OK && fixture.closes==1u);
    assert(fx_overlay_file_open(&store,consumer,binding,&handle,&bytes)==FX_EMPTY);
    assert(fx_overlay_publish(&store,publisher,job.bulk.stage,1u,41u)==FX_CAPABILITY);
    assert(fx_overlay_publish(&store,publisher,job.bulk.stage,1u,42u)==FX_OK);
    assert(fx_overlay_file_open(&store,consumer,"/system/build.prop.bak",&handle,&bytes)==FX_EMPTY);
    assert(fx_overlay_file_open(&store,consumer,binding,&handle,&bytes)==FX_OK);
    assert(bytes==sizeof(replacement));

    FxOverlayCardLoad corrupt={0};
    image.revision=2u;
    image.replacement_sha256[0]^=1u;
    assert(fx_overlay_card_begin(&corrupt,&store,publisher,&source,"build.prop",&image)==FX_OK);
    assert(load(&corrupt)==FX_INTEGRITY && fixture.closes==2u && fixture.frees==1u);
    assert(store.phase==FX_OVERLAY_ACTIVE && store.retained==sizeof(replacement));
    uint64_t new_handle=0u;
    assert(fx_overlay_file_open(&store,consumer,binding,&new_handle,&bytes)==FX_OK);
    assert(fx_overlay_close(&store,consumer,new_handle)==FX_OK);

    assert(fx_overlay_restore(&store,publisher,1u,2u,42u)==FX_OK);
    assert(fx_overlay_file_open(&store,consumer,binding,&new_handle,&bytes)==FX_EMPTY);
    assert(fixture.frees==1u);
    assert(fx_overlay_seek(&store,consumer,handle,5,0u,&bytes)==FX_OK && bytes==5u);
    uint8_t buffer[32];
    assert(fx_overlay_read(&store,consumer,handle,buffer,sizeof(buffer),&bytes)==FX_OK);
    assert(bytes==sizeof(replacement)-5u && !memcmp(buffer,replacement+5u,bytes));
    assert(fx_overlay_close(&store,consumer,handle)==FX_OK && fixture.frees==2u);
    assert(!memcmp(original,"mode=original\n",sizeof(original)));
    return 0;
}
