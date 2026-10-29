#include "fenestra_scene.h"
#include "fenestra_memory.h"
void fx_scene_init(FxSceneStore *s,FxSceneCommand *first,FxSceneCommand *second) {
    memset(s,0,sizeof(*s));s->front=1u;
    s->scene[0].commands=first;s->scene[1].commands=second;
    s->scene[0].capacity=first?FX_SCENE_COMMANDS:0u;
    s->scene[1].capacity=second?FX_SCENE_COMMANDS:0u;
}
FxResult fx_scene_validate(const FxScene *s) {
    if(!s || !s->commands || !s->width || !s->height || s->width>FX_SCENE_EXTENT || s->height>FX_SCENE_EXTENT ||
       !s->count || s->count>s->capacity || s->capacity>FX_SCENE_COMMANDS)return FX_BOUNDS;
    if(s->commands[0].kind!=FX_SCENE_CLEAR)return FX_INTEGRITY;
    for(uint32_t i=0;i<s->count;++i) {
        const FxSceneCommand *c=&s->commands[i];
        if(c->kind!=FX_SCENE_IMAGE && c->rgb>0xffffffu)return FX_BOUNDS;
        if(c->kind==FX_SCENE_CLEAR) {
            if(i || c->x || c->y || c->glyph || c->scale)return FX_INTEGRITY;
        } else {
            uint32_t w,h;
            if(c->kind==FX_SCENE_GLYPH) {
                if(c->glyph<32u || c->glyph>126u || !c->scale || c->scale>32u)return FX_BOUNDS;
                w=5u*c->scale;h=7u*c->scale;
            } else if(c->kind==FX_SCENE_RECT || c->kind==FX_SCENE_IMAGE) {
                w=c->glyph;h=c->scale;if(!w || !h)return FX_BOUNDS;
                if(c->kind==FX_SCENE_IMAGE) {
                    if(!s->data || s->data_bytes>s->data_capacity || s->data_bytes>FX_SCENE_DATA_BYTES ||
                       (c->rgb&3u) || c->rgb>s->data_bytes || sizeof(FxSceneImage)>s->data_bytes-c->rgb)return FX_BOUNDS;
                    FxSceneImage image;memcpy(&image,s->data+c->rgb,sizeof(image));
                    if(image.format!=FX_SCENE_RGBA8 || image.reserved)return FX_ARGUMENT;
                    if(!image.width || !image.height || image.width>FX_SCENE_EXTENT || image.height>FX_SCENE_EXTENT ||
                       (image.pixels&3u) || (image.stride&3u) || image.stride>INT32_MAX || image.stride<image.width*4u ||
                       image.pixels>s->data_bytes ||
                       (uint64_t)(image.height-1u)*image.stride+image.width*4u>s->data_bytes-image.pixels)return FX_BOUNDS;
                }
            }
            else return FX_ARGUMENT;
            if(c->x>=s->width || c->y>=s->height || w>s->width-c->x || h>s->height-c->y)return FX_BOUNDS;
        }
    }
    return FX_OK;
}
static FxResult stage(FxSceneStore *s,FxPlatformOwner *o,const FxSceneSubmission *r,uint64_t data,uint32_t bytes) {
    if(!s || !r)return FX_ARGUMENT;
    s->ready=0u;
    if(s->frames==UINT32_MAX)return FX_BUDGET;
    if(r->abi!=FX_SCENE_ABI || !r->page)return FX_ARGUMENT;
    if(!r->width || !r->height || r->width>FX_SCENE_EXTENT || r->height>FX_SCENE_EXTENT ||
       !r->count || r->count>FX_SCENE_COMMANDS)return FX_BOUNDS;
    uint32_t at=s->front^1u;FxScene *next=&s->scene[at];
    if(!next->commands || next->capacity<r->count || next->capacity>FX_SCENE_COMMANDS)return FX_STATE;
    FxResult result=fx_platform_module_transfer(o,r->commands,next->commands,r->count*(uint32_t)sizeof(FxSceneCommand),0u);
    if(result!=FX_OK)return result;
    next->data_bytes=0u;
    if(bytes) {
        if(!next->data || bytes>next->data_capacity || bytes>FX_SCENE_DATA_BYTES)return FX_BOUNDS;
        result=fx_platform_module_transfer(o,data,next->data,bytes,0u);
        if(result!=FX_OK)return result;
        next->data_bytes=bytes;
    }
    next->width=r->width;next->height=r->height;next->count=r->count;
    result=fx_scene_validate(next);
    if(result==FX_OK){s->staged=at;s->ready=1u;}
    return result;
}
FxResult fx_scene_stage(FxSceneStore *s,FxPlatformOwner *o,const FxSceneSubmission *r) {
    if(!r || r->bytes!=sizeof(*r))return FX_ARGUMENT;
    return stage(s,o,r,0u,0u);
}
FxResult fx_scene_stage_images(FxSceneStore *s,FxPlatformOwner *o,const FxSceneImageSubmission *r) {
    if(!r || r->base.bytes!=sizeof(*r) || r->reserved)return FX_ARGUMENT;
    if(!r->data_bytes || r->data_bytes>FX_SCENE_DATA_BYTES)return FX_BOUNDS;
    return stage(s,o,&r->base,r->data,r->data_bytes);
}
const FxScene *fx_scene_candidate(const FxSceneStore *s){return s && s->ready?&s->scene[s->staged]:0;}
void fx_scene_commit(FxSceneStore *s){if(s && s->ready){s->front=s->staged;s->ready=0u;++s->frames;}}
