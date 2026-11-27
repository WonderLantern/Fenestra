#include "fenestra_overlay_file.h"

FxResult fx_overlay_file_open(FxOverlay *store,FxOverlayActor actor,const char *binding,
                              uint64_t *handle,uint32_t *bytes) {
    if(handle)*handle=0u;
    if(bytes)*bytes=0u;
    if(!store || !binding || !*binding || !handle || !bytes)return FX_ARGUMENT;
    for(uint32_t i=0;i<store->target_count;++i) {
        const FxOverlayTargetSpec *spec=&store->targets[i].spec;
        uint32_t j=0;
        while(j<sizeof(spec->binding) && spec->binding[j] && binding[j]==spec->binding[j])
            ++j;
        if(j<sizeof(spec->binding) && !spec->binding[j] && !binding[j])
            return fx_overlay_open(store,actor,spec->id,0u,handle,bytes);
    }
    return FX_EMPTY;
}

static FxResult card_path(const char *directory,const char *name,char *path,uint32_t capacity) {
    if(!directory || !name || !*name || *name=='.')return FX_ARGUMENT;
    uint32_t n=0u;
    while(n<capacity && directory[n]) {
        path[n]=directory[n];
        ++n;
    }
    if(!n || n==capacity || (path[n-1u]!='/' && path[n-1u]!='\\'))return FX_ARGUMENT;
    for(uint32_t i=0u;name[i];++i) {
        if(i>=63u || n+1u>=capacity)return FX_BOUNDS;
        const char c=name[i];
        if((c<'A' || c>'Z') && (c<'a' || c>'z') && (c<'0' || c>'9') &&
           c!='_' && c!='-' && c!='.')return FX_ARGUMENT;
        if(c=='.' && (name[i+1u]=='.' || !name[i+1u]))return FX_ARGUMENT;
        path[n++]=c;
    }
    path[n]=0;
    return FX_OK;
}

static FxResult card_read(void *context,void *data,uint32_t bytes,uint32_t *actual) {
    FxOverlayCardLoad *job=context;
    if(!job->open || job->close_attempted)return FX_STATE;
    return job->files.read(job->files.context,job->fd,data,bytes,actual)?FX_IO:FX_OK;
}

static FxResult card_close(void *context) {
    FxOverlayCardLoad *job=context;
    if(!job->open || job->close_attempted)return FX_STATE;
    job->close_attempted=1u;
    if(job->files.close(job->files.context,job->fd))return FX_CLOSE;
    job->open=0u;
    return FX_OK;
}

FxResult fx_overlay_card_begin(FxOverlayCardLoad *job,FxOverlay *store,FxOverlayActor actor,
    const FxOverlayCardSource *source,const char *name,const FxOverlayCardImage *image) {
    if(!job || !store || !actor.owner || !source || !image || !source->files.open ||
       !source->files.read || !source->files.close)return FX_ARGUMENT;
    if(job->open || job->bulk.store)return FX_BUSY;
    if(store->phase!=FX_OVERLAY_ACTIVE)return FX_STATE;
    if(actor.generation!=store->generation)return FX_STALE;

    char path[192];
    FxResult result=card_path(source->directory,name,path,sizeof(path));
    if(result!=FX_OK)return result;
    job->files=source->files;
    job->close_attempted=0u;
    job->fd=job->files.open(job->files.context,path,FX_TEXT_READ);
    if(job->fd<0)return (uint32_t)job->fd==source->missing_error?FX_EMPTY:FX_IO;
    job->open=1u;

    uint64_t stage=0u;
    result=fx_overlay_begin(store,actor,image->target,image->revision,image->bytes,
        image->original_sha256,image->replacement_sha256,&stage);
    if(result==FX_OK) {
        const FxOverlaySource reader={.context=job,.read=card_read,.close=card_close};
        result=fx_overlay_bulk_init(&job->bulk,store,actor,stage,reader);
        if(result==FX_OK)return FX_OK;
    }
    if(card_close(job)!=FX_OK)return fx_overlay_panic(store,FX_CLOSE);
    if(stage && store->phase==FX_OVERLAY_ACTIVE) {
        const FxResult cleanup=fx_overlay_abort(store,actor,stage);
        if(cleanup!=FX_OK)return cleanup;
    }
    return result;
}

FxResult fx_overlay_card_reset(FxOverlayCardLoad *job) {
    if(!job)return FX_ARGUMENT;
    if(job->open)return FX_BUSY;
    if(job->bulk.store) {
        FxOverlay *store=job->bulk.store;
        if(store->phase==FX_OVERLAY_FAULT)return FX_STATE;
        for(uint32_t i=0u;i<FX_OVERLAY_VERSIONS;++i) {
            const FxOverlayVersion *version=&store->versions[i];
            if(version->id==job->bulk.stage &&
               (version->state==FX_OVERLAY_WRITING || version->state==FX_OVERLAY_READY))
                return FX_BUSY;
        }
    }
    *job=(FxOverlayCardLoad){0};
    return FX_OK;
}
