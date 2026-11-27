#include "fenestra_overlay_ranges.h"
#include "fenestra_memory.h"

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t wide(const uint8_t *p) {
    return word(p)|(uint64_t)word(p+4)<<32;
}

/* Require canonical contiguous tables/data and sorted disjoint ranges. Each
 * version can use different coordinates, size, hashes and replacement bytes. */
static FxResult check_recipe(const uint8_t *p,uint32_t bytes) {
    if(bytes<FX_RANGE_HEADER_BYTES || memcmp(p,"FNR1",4u) ||
       word(p+4)!=1u || word(p+8)!=bytes)return FX_INTEGRITY;
    uint32_t versions=word(p+12);
    if(!versions || versions>(bytes-FX_RANGE_HEADER_BYTES)/FX_RANGE_VERSION_BYTES)return FX_BOUNDS;
    uint32_t cursor=FX_RANGE_HEADER_BYTES+versions*FX_RANGE_VERSION_BYTES;
    for(uint32_t i=0;i<versions;++i) {
        const uint8_t *v=p+FX_RANGE_HEADER_BYTES+i*FX_RANGE_VERSION_BYTES;
        uint32_t length=word(v+104),count=word(v+108),table=word(v+112);
        if(!length || wide(v+96)>UINT64_MAX-length || word(v+116) || !count ||
           table!=cursor || count>(bytes-cursor)/FX_RANGE_SEGMENT_BYTES)return FX_BOUNDS;
        for(uint32_t j=0;j<i;++j)
            if(!memcmp(v,p+FX_RANGE_HEADER_BYTES+j*FX_RANGE_VERSION_BYTES,32u))return FX_INTEGRITY;
        cursor+=count*FX_RANGE_SEGMENT_BYTES;
        uint32_t previous=0u;
        for(uint32_t j=0;j<count;++j) {
            const uint8_t *segment=p+table+j*FX_RANGE_SEGMENT_BYTES;
            uint32_t start=word(segment),end=word(segment+4),data=word(segment+8),reserved=word(segment+12);
            if(start<previous || start>=end || end>length || reserved || data!=cursor ||
               end-start>bytes-cursor)return FX_BOUNDS;
            previous=end;
            cursor+=end-start;
        }
    }
    return cursor==bytes?FX_OK:FX_INTEGRITY;
}

FxResult fx_overlay_ranges_init(FxOverlayRangeView *view,const void *recipe,uint32_t bytes,
                                 const FxOverlayRangeOriginal *source) {
    if(!view || !recipe || !source || !source->data || !source->bytes ||
       source->offset>UINT64_MAX-source->bytes ||
       (uintptr_t)source->data>UINTPTR_MAX-source->bytes ||
       (uintptr_t)recipe>UINTPTR_MAX-bytes)return FX_ARGUMENT;
    *view=(FxOverlayRangeView){0};
    const uint8_t *p=recipe;
    FxResult result=check_recipe(p,bytes);
    if(result!=FX_OK)return result;
    for(uint32_t i=0;i<word(p+12);++i) {
        const uint8_t *v=p+FX_RANGE_HEADER_BYTES+i*FX_RANGE_VERSION_BYTES;
        if(memcmp(v,source->application_sha256,32u))continue;
        if(wide(v+96)!=source->offset || word(v+104)!=source->bytes)return FX_INTEGRITY;
        *view=(FxOverlayRangeView){.recipe=p,.recipe_bytes=bytes,.version=v,
            .original=source->data,.bytes=source->bytes,.segments=word(v+112),.segment_count=word(v+108)};
        fx_sha256_init(&view->original_hash);
        fx_sha256_init(&view->result_hash);
        return FX_OK;
    }
    return FX_EMPTY;
}

/* Return one contiguous span from the original or a replacement. Binary
 * search keeps seeks practical for recipes with many independent segments. */
static const uint8_t *span(const FxOverlayRangeView *view,uint32_t at,uint32_t *length) {
    uint32_t low=0u,high=view->segment_count;
    while(low<high) {
        uint32_t mid=low+(high-low)/2u;
        const uint8_t *segment=view->recipe+view->segments+mid*FX_RANGE_SEGMENT_BYTES;
        if(word(segment+4)<=at)low=mid+1u;
        else high=mid;
    }
    uint32_t end=view->bytes;
    if(low<view->segment_count) {
        const uint8_t *segment=view->recipe+view->segments+low*FX_RANGE_SEGMENT_BYTES;
        uint32_t start=word(segment);
        if(at>=start) {
            *length=word(segment+4)-at;
            return view->recipe+word(segment+8)+(at-start);
        }
        end=start;
    }
    *length=end-at;
    return view->original+at;
}

FxResult fx_overlay_ranges_verify(FxOverlayRangeView *view) {
    if(!view || !view->version)return FX_STATE;
    if(view->verified==1u)return FX_OK;
    if(view->verified==2u)return FX_INTEGRITY;
    uint32_t budget=view->bytes-view->verify_offset;
    if(budget>FX_RANGE_VERIFY_STEP)budget=FX_RANGE_VERIFY_STEP;
    FxResult result=fx_sha256_update(&view->original_hash,view->original+view->verify_offset,budget);
    if(result!=FX_OK)return result;
    while(budget) {
        uint32_t count;
        const uint8_t *data=span(view,view->verify_offset,&count);
        if(count>budget)count=budget;
        result=fx_sha256_update(&view->result_hash,data,count);
        if(result!=FX_OK)return result;
        budget-=count;
        view->verify_offset+=count;
    }
    if(view->verify_offset<view->bytes)return FX_BUSY;
    uint8_t original[32],output[32];
    fx_sha256_final(&view->original_hash,original);
    fx_sha256_final(&view->result_hash,output);
    if(memcmp(original,view->version+32u,32u) || memcmp(output,view->version+64u,32u)) {
        view->verified=2u;
        return FX_INTEGRITY;
    }
    view->verified=1u;
    return FX_OK;
}

FxResult fx_overlay_ranges_read(const FxOverlayRangeView *view,uint32_t offset,
                                void *destination,uint32_t bytes,uint32_t *actual) {
    if(actual)*actual=0u;
    if(!view || !actual || (!destination && bytes))return FX_ARGUMENT;
    if(view->verified!=1u)return FX_STATE;
    if(offset>view->bytes)return FX_BOUNDS;
    if(bytes>view->bytes-offset)bytes=view->bytes-offset;
    uintptr_t target=(uintptr_t)destination;
    if(target>UINTPTR_MAX-bytes)return FX_ARGUMENT;
    /* A caller cannot use read to overwrite its source recipe or original. */
    if(bytes && ((target<(uintptr_t)view->original+view->bytes && (uintptr_t)view->original<target+bytes) ||
       (target<(uintptr_t)view->recipe+view->recipe_bytes && (uintptr_t)view->recipe<target+bytes) ||
       (target<(uintptr_t)view+sizeof(*view) && (uintptr_t)view<target+bytes)))return FX_ARGUMENT;
    uint32_t copied=0u;
    while(copied<bytes) {
        uint32_t count;
        const uint8_t *data=span(view,offset+copied,&count);
        if(count>bytes-copied)count=bytes-copied;
        memcpy((uint8_t *)destination+copied,data,count);
        copied+=count;
    }
    *actual=copied;
    return FX_OK;
}
