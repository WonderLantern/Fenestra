#include "fenestra_native_font.h"
#include "fenestra_memory.h"

static FxResult text_bank(FxIndexMemory memory,const FxNativeTextBank *old,const uint8_t *fonts,
        const uintptr_t *attributes,uint32_t stock,uint32_t languages,uint32_t required,
        uint8_t font,uint8_t attribute,FxNativeTextBank **out) {
    if(!out || *out || !memory.allocate || !memory.release || !fonts || !attributes ||
       !stock || !languages || languages>32u || required<stock || required>FX_NATIVE_TEXT_LIMIT)return FX_ARGUMENT;
    for(uint32_t i=0;i<languages;++i)if(!attributes[i])return FX_ARGUMENT;
    uint32_t extra=required-stock,room=16u;
    while(room<extra)room*=2u;
    uint32_t capacity=stock+(room>FX_NATIVE_TEXT_LIMIT-stock?FX_NATIVE_TEXT_LIMIT-stock:room);
    uint32_t roots=(uint32_t)((sizeof(FxNativeTextBank)+7u)&~(size_t)7u);
    uint32_t pointers=roots+languages*(uint32_t)sizeof(uintptr_t);
    uint32_t bytes=pointers+capacity*(languages+1u),retained=old?old->retained_bytes:0u;
    if(retained>FX_NATIVE_TEXT_BUDGET || bytes>FX_NATIVE_TEXT_BUDGET-retained)return FX_BUDGET;
    FxNativeTextBank *bank=memory.allocate(memory.context,bytes);if(!bank)return FX_BUDGET;
    memset(bank,0,bytes);bank->previous=old;bank->stock=stock;bank->capacity=capacity;
    bank->languages=languages;bank->bytes=bytes;bank->retained_bytes=retained+bytes;
    bank->font_default=font;bank->attribute_default=attribute;
    bank->attributes=(uintptr_t *)((uint8_t *)bank+roots);
    bank->fonts=(uint8_t *)bank+pointers;
    uint32_t copied=old?old->capacity:stock;
    memcpy(bank->fonts,fonts,copied);memset(bank->fonts+copied,font,capacity-copied);
    for(uint32_t i=0;i<languages;++i) {
        uint8_t *row=bank->fonts+capacity*(i+1u);bank->attributes[i]=(uintptr_t)row;
        memcpy(row,(const void *)attributes[i],copied);memset(row+copied,attribute,capacity-copied);
    }
    *out=bank;return FX_OK;
}
FxResult fx_native_text_bank_create(FxIndexMemory memory,const uint8_t *fonts,const uintptr_t *attributes,
        uint32_t stock,uint32_t languages,uint32_t required,uint8_t font,uint8_t attribute,FxNativeTextBank **out) {
    return text_bank(memory,0,fonts,attributes,stock,languages,required,font,attribute,out);
}
FxResult fx_native_text_bank_grow(FxIndexMemory memory,FxNativeTextBank *old,uint32_t required,FxNativeTextBank **out) {
    if(!out || *out || !old)return FX_ARGUMENT;
    if(required<=old->capacity){*out=old;return FX_OK;}
    return text_bank(memory,old,old->fonts,old->attributes,old->stock,old->languages,required,
        old->font_default,old->attribute_default,out);
}

void fx_native_font_enter(FxNativeFont *s, const FxNativeFontProfile *p,
                          const FxTextRegions *regions,const void *request) {
    ((void (*)(void))p->lock)();
    if (!s)
        return;
    s->active = 0u;
    if (!request || !regions)
        return;
    /* All adapted requests take the original font configuration, whose
     * source text pointer is at +0x20. No module address is dereferenced. */
    uintptr_t text = *(const uintptr_t *)((const uint8_t *)request + 0x20u);
    if (!text)
        return;
    /* Retained core UI storage also covers queued requests/substrings after
     * a row changes. No per-ID scan, publication, candidate-text read or heap. */
    if((text&1u) || !fx_text_regions_contains(regions,text,sizeof(uint16_t),0))return;
    uintptr_t engine = *(const uintptr_t *)p->engine_root;
    if (!engine)
        return;
    /* The native engine and its font contexts live for the camera session.
     * Keep the fallback context alive while native layouts can reference it. */
    if (s->engine != engine) {
        s->engine = engine;
        s->face = 0u;
    }
    if (!s->face)
        s->face = ((uintptr_t (*)(uintptr_t, uint32_t, uintptr_t, uint32_t))p->open_font)(
            engine, 0u, p->fallback_data, p->fallback_bytes);
    if (!s->face) {
        s->error = 1u;
        return;
    }
    /* A fixed last slot keeps cached glyph indices stable across requests.
     * The original primary font and earlier fallback priorities are retained.
     * Restore the previous last slot before releasing the native mutex. */
    s->saved = *(const uintptr_t *)(engine + 0x100u);
    uint32_t result = ((uint32_t (*)(uintptr_t, uintptr_t, uint32_t))p->select_font)(engine, s->face, 3u);
    s->error = result;
    s->active = result == 0u;
}

void fx_native_font_leave(FxNativeFont *s, const FxNativeFontProfile *p) {
    if (s && s->active) {
        s->error = ((uint32_t (*)(uintptr_t, uintptr_t, uint32_t))p->select_font)(s->engine, s->saved, 3u);
        s->active = 0u;
    }
    ((void (*)(void))p->unlock)();
}

uint32_t fx_native_font_line_metrics(FxNativeFont *s, const FxNativeFontProfile *p,
                                      uintptr_t engine, uintptr_t layout, int32_t dimensions[2]) {
    uint32_t (*measure)(uintptr_t,uintptr_t,int32_t *)=(void *)p->measure;
    uint32_t result=measure(engine,layout,dimensions);
    if(!s || !s->active || result || dimensions[1] || !layout)return result;
    /* This one original call probes the minimum line height. A fallback glyph
     * taller than that box produces a successful empty result; treating zero
     * as glyph height adds the whole minimum as unwanted leading. Retry only
     * this clipped probe, with the original width and a full signed 16.16
     * height range. Ordinary rendering/measurement bounds stay unchanged. */
    const int32_t *bounds=(const int32_t *)(layout+0x10u);
    int32_t saved[2]={bounds[0],bounds[1]},expanded[2]={bounds[0],INT32_MAX};
    if(saved[1]<=0 || saved[1]==INT32_MAX)return result;
    uint32_t (*set)(uintptr_t,uint32_t,const int32_t *)=(void *)p->set_layout;
    if(set(layout,1u,expanded))return result;
    int32_t natural[2]={0,0};uint32_t retry=measure(engine,layout,natural);
    (void)set(layout,1u,saved);
    if(!retry && natural[1]>saved[1]) {dimensions[0]=natural[0];dimensions[1]=natural[1];}
    return result;
}
