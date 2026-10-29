/* Native entry wrappers. Original producers execute through checked displaced
 * instruction trampolines. Original ROM and shared profile tables are never
 * mutated. The experimental capture_menu owner links these adapters only in
 * explicit test builds pending complete transition/recording qualification. */
#include "fenestra_c110_capture_hooks.h"
#include "fenestra_memory.h"

extern uint64_t fx_c110_original_capture_source(uint64_t,const uint32_t *,void *);
extern uint64_t fx_c110_original_capture_geometry(const uint32_t *,void *);
extern uint64_t fx_c110_original_capture_cpc(const uint32_t *,void *);
extern uint64_t fx_c110_original_capture_raw_source(const uint32_t *,void *);
extern uint64_t fx_c110_original_capture_raw_stored(const uint32_t *,void *);
extern uint64_t fx_c110_original_capture_active(const uint32_t *,void *);
extern uint64_t fx_c110_original_capture_nrmt(const uint32_t *,void *);
extern uint64_t fx_c110_original_capture_file(const uint32_t *,void *);
extern uint64_t fx_c110_original_capture_movie_metadata(const uint32_t *,void *);
extern const void *fx_c110_original_capture_large(const uint32_t *);
extern const void *fx_c110_original_capture_small(const uint32_t *);
extern const void *fx_c110_original_capture_picture(uint32_t,uint32_t);
extern uint64_t fx_c110_original_capture_picture_copy(uint32_t,uint32_t,void *);
extern uint64_t fx_c110_original_capture_sensor(const uint32_t *);
extern uint64_t fx_c110_original_capture_pool(const uint32_t *,uint32_t);

static void fault(FxC110CaptureHooks *s,FxResult error) {
    uint32_t zero=0u;
    (void)atomic_compare_exchange_strong(&s->fault,&zero,(uint32_t)error);
}
static const FxCaptureProfile *profile_mode(FxC110CaptureHooks *s,uint32_t mode) {
    if(!mode)return 0;
    for(uint32_t i=0;i<s->configuration->count;++i)if(s->profiles[i].mode_id==mode)return &s->profiles[i];
    fault(s,FX_INTEGRITY);return 0;
}
static const FxCaptureProfile *selected(FxC110CaptureHooks **owner) {
    FxC110CaptureHooks *s=fx_c110_capture_hooks();*owner=s;
    if(!s || !atomic_load_explicit(&s->prepared,memory_order_acquire))return 0;
    if(s->handoff && !s->handoff())return 0;
    uint32_t mode=atomic_load_explicit(&s->mode,memory_order_acquire);
    return profile_mode(s,mode);
}
static const FxVideoConfigurationMode *binding(const FxC110CaptureHooks *s,const FxCaptureProfile *p) {
    if(!s || !p)return 0;
    for(uint32_t i=0;i<s->configuration->count;++i)
        if(s->profiles[i].mode_id==p->mode_id)return &s->configuration->modes[i];
    return 0;
}
static uint32_t mode_index(const FxC110CaptureHooks *s,const FxCaptureProfile *p) {
    return (uint32_t)(binding(s,p)-s->configuration->modes);
}
static uint32_t rate(const FxCaptureProfile *p) {
    const FxVideoConfigurationMode *m=binding(fx_c110_capture_hooks(),p);return m?m->native_rate:UINT32_MAX;
}
static const FxCaptureProfile *display_selected(FxC110CaptureHooks **owner) {
    FxC110CaptureHooks *s=fx_c110_capture_hooks();*owner=s;
    if(!s || !atomic_load_explicit(&s->prepared,memory_order_acquire) || atomic_load(&s->fault))return 0;
    /* Display/aspect queries can run from the native owner's release path.
     * They must never release/rebuild that owner or take the transaction gate.
     * Only read the prospective display mode; capture ownership stays native. */
    uint32_t mode;
    /* A callback that loses its pending epoch must not return a stale profile
     * sampled before the concurrent committed-mode publication. */
    if(!s->display_mode || !s->display_mode(&mode))mode=atomic_load_explicit(&s->mode,memory_order_acquire);
    return profile_mode(s,mode);
}
static int image_context(const FxCaptureProfile *p,const uint32_t *q) {
    return p && q && (q[0]==10u || q[0]==12u) && q[1]==(binding(fx_c110_capture_hooks(),p)->native_layout&65535u) && q[2]==rate(p);
}
static int size_context(const FxCaptureProfile *p,const uint32_t *q) {
    const FxVideoConfigurationMode *m=binding(fx_c110_capture_hooks(),p);
    return m && q && !q[0] && q[1]==3u && q[3]==(m->native_layout&65535u) &&
        q[8]==m->native_codec && q[9]==(m->native_layout&65535u);
}
static int frame_context(const FxCaptureProfile *p,const uint32_t *q) {
    const FxVideoConfigurationMode *m=binding(fx_c110_capture_hooks(),p);
    return m && q && q[0]==1u && q[2]==3u && q[0x388u/4u]==(m->native_layout&65535u) && q[0x39cu/4u]==m->native_codec;
}
static int project(FxC110CaptureHooks *s,const FxCaptureProfile *p,uint32_t kind,void *out,uint32_t n) {
    FxResult r=fx_c110_capture_project(binding(s,p),kind,out,n);
    if(r!=FX_OK) {
        fault(s,r);
        return 0;
    }
    atomic_fetch_add(&s->projected,1u);return 1;
}
FxResult fx_c110_capture_hooks_prepare(FxC110CaptureHooks *s,const void *data,uint32_t bytes) {
    if(!s || !data || !s->configuration)return FX_ARGUMENT;
    if(s->prepared || atomic_load(&s->mode))return FX_STATE;
    FxResult r=fx_video_configuration_check(data,bytes);if(r!=FX_OK)return r;
    const FxVideoConfigurationData *p=data;
    static const uint32_t sizes[FX_VIDEO_RECORDS]={48,108,4,4,4,16,5,24,376,56,4,2};
    /* This adapter binds Nikon's R3D producer layout. The common profile
     * contract accepts other codecs; their native record ABIs need bindings. */
    for(uint32_t m=0;m<p->count;++m)
        if(p->modes[m].profile.codec!=FX_CAPTURE_CODEC_R3D || p->modes[m].native_codec!=7u)return FX_CAPABILITY;
    for(uint32_t m=0;m<p->count;++m)for(uint32_t i=0;i<FX_VIDEO_RECORDS;++i)
        if(p->modes[m].records[i].bytes!=sizes[i])return FX_ARGUMENT;
    /* Obtain the original immutable records through their native ABI. Their
     * addresses follow the requested native image/rate context. */
    for(uint32_t m=0;m<p->count;++m) {
        const FxVideoConfigurationMode *mode=&p->modes[m];
        for(uint32_t i=0;i<2u;++i) {
            const uint32_t context[4]={i?12u:10u,mode->native_layout&65535u,mode->native_rate,mode->native_codec};
            uintptr_t large=(uintptr_t)fx_c110_original_capture_large(context);
            uintptr_t small=(uintptr_t)fx_c110_original_capture_small(context);
            if(!large || !small)return FX_CAPABILITY;
            s->large_source[m][i]=large;s->small_source[m][i]=small;
            memcpy(s->large[m][i],(const void *)large,sizeof(s->large[m][i]));
            memcpy(s->small[m][i],(const void *)small,sizeof(s->small[m][i]));
            r=fx_c110_capture_project(mode,FX_C110_PREVIEW_PROFILE,s->large[m][i],sizeof(s->large[m][i]));
            if(r!=FX_OK)return r;
            r=fx_c110_capture_project(mode,FX_C110_PREVIEW_DIMENSIONS,s->small[m][i],sizeof(s->small[m][i]));
            if(r!=FX_OK)return r;
        }
        uintptr_t picture=(uintptr_t)fx_c110_original_capture_picture(mode->native_layout&65535u,0u);
        if(!picture)return FX_CAPABILITY;
        s->picture_source[m]=picture;memcpy(s->picture[m],(const void *)picture,4u);
        r=fx_c110_capture_project(mode,FX_C110_PICTURE_SIZE,s->picture[m],4u);if(r!=FX_OK)return r;
    }
    if(p!=s->configuration)memcpy(s->configuration,p,bytes);
    for(uint32_t i=0;i<p->count;++i)s->profiles[i]=p->modes[i].profile;
    /* Publication belongs to the native owner, after its selector copies and
     * callback pointers are complete. Until then every wrapper stays stock. */
    return FX_OK;
}
uint64_t fx_c110_capture_source(uint64_t kind,const uint32_t *q,void *out) {
    uint64_t result=fx_c110_original_capture_source(kind,q,out);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s);
    if(image_context(p,q) && !project(s,p,FX_C110_SOURCE,out,48u))return 0u;
    return result;
}
uint64_t fx_c110_capture_geometry(const uint32_t *q,void *out) {
    uint64_t result=fx_c110_original_capture_geometry(q,out);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s);
    if(image_context(p,q) && !project(s,p,FX_C110_GEOMETRY,out,108u))return 0u;
    return result;
}
#define SIZE_WRAPPER(name,kind) \
uint64_t fx_c110_capture_##name(const uint32_t *q,void *out) { \
    uint64_t result=fx_c110_original_capture_##name(q,out); \
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s); \
    if(size_context(p,q) && !project(s,p,kind,out,4u))return 0u; \
    return result; \
}
SIZE_WRAPPER(cpc,FX_C110_CPC_SIZE)
SIZE_WRAPPER(raw_source,FX_C110_RAW_SOURCE_SIZE)
SIZE_WRAPPER(raw_stored,FX_C110_RAW_STORED_SIZE)
#define FRAME_WRAPPER(name,kind,n) \
uint64_t fx_c110_capture_##name(const uint32_t *q,void *out) { \
    uint64_t result=fx_c110_original_capture_##name(q,out); \
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s); \
    if(frame_context(p,q) && !project(s,p,kind,out,n))return 0u; \
    return result; \
}
FRAME_WRAPPER(active,FX_C110_ACTIVE_AREA,16u)
FRAME_WRAPPER(nrmt,FX_C110_NRMT_HEIGHT,5u)
uint64_t fx_c110_capture_file(const uint32_t *q,void *out) {
    uint64_t result=fx_c110_original_capture_file(q,out);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s);
    if(p && q && q[0]==3u && q[1]==0u && q[2]==binding(s,p)->file_rate &&
       !project(s,p,FX_C110_R3D_RECORD,out,24u))return 0u;
    return result;
}
uint64_t fx_c110_capture_movie_metadata(const uint32_t *q,void *out) {
    uint64_t result=fx_c110_original_capture_movie_metadata(q,out);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s);
    /* Actual NCTG0x23 callback in the pinned C1.10 tag table. The original
     * flag at+15c selects auxiliary/proxy metadata; it MUST remain stock.
     * Resolution7 is the full-width RAW raster. A published/drained native
     * module mode lease is also required. This producer owns only the halfword
     * at+a6: do not inspect or mutate other callbacks' output fields. */
    if(p && q && out && (result&1u) && !((const uint8_t *)q)[0x15cu] && q[3]==binding(s,p)->native_family &&
       !project(s,p,FX_C110_NCTG_HEIGHT,(uint8_t *)out+0xa6u,2u))return 0u;
    return result;
}
static const void *profile(FxC110CaptureHooks *s,const FxCaptureProfile *p,
                           const uint32_t *q,const void *original,int large) {
    if(!image_context(p,q))return original;
    uint32_t m=mode_index(s,p);
    for(uint32_t i=0;i<2u;++i)if((uintptr_t)original==(large?s->large_source[m][i]:s->small_source[m][i]))
        return large?(const void *)s->large[m][i]:(const void *)s->small[m][i];
    fault(s,FX_INTEGRITY);
    return 0;
}
const void *fx_c110_capture_large(const uint32_t *q) {
    const void *result=fx_c110_original_capture_large(q);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s);return profile(s,p,q,result,1);
}
const void *fx_c110_capture_small(const uint32_t *q) {
    const void *result=fx_c110_original_capture_small(q);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s);return profile(s,p,q,result,0);
}
const void *fx_c110_capture_picture(uint32_t group,uint32_t item) {
    const void *result=fx_c110_original_capture_picture(group,item);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=display_selected(&s);
    if(p && group==(binding(s,p)->native_layout&65535u) && item==0u) {
        if((uintptr_t)result!=s->picture_source[mode_index(s,p)]) {fault(s,FX_INTEGRITY);return 0;}
        return s->picture[mode_index(s,p)];
    }
    return result;
}
uint64_t fx_c110_capture_picture_copy(uint32_t group,uint32_t area,void *out) {
    uint64_t result=fx_c110_original_capture_picture_copy(group,area,out);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=display_selected(&s);
    if(p && group==(binding(s,p)->native_layout&65535u) && area==(binding(s,p)->native_layout>>16) && !project(s,p,FX_C110_PICTURE_SIZE,out,4u))return 0u;
    return result;
}
uint64_t fx_c110_capture_sensor(const uint32_t *q) {
    uint64_t result=fx_c110_original_capture_sensor(q);
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s);(void)s;
    if(p && q && q[0]==5u && q[1]==3u && q[5]==binding(s,p)->native_family)return p->sensor_mode;
    return result;
}

uint64_t fx_c110_capture_pool(const uint32_t *q,uint32_t count) {
    FxC110CaptureHooks *s;const FxCaptureProfile *p=selected(&s);
    if(!frame_context(p,q))return fx_c110_original_capture_pool(q,count);
    /* 41244890's original R3D branch creates a child region inside resource80.
     * The master region remains stock (1,086,873,600 bytes). Only this native
     * child descriptor changes. Original40ee880c derives indexed addresses
     * from its unit/count, not from a changed global ROM allocation table. */
    typedef struct { uint64_t base;uint32_t total,reserved,unit,count; } Child;
    _Static_assert(sizeof(Child)==24u,"C1.10 resource child descriptor");
#define NATIVE(a,t) ((t)(uintptr_t)UINT64_C(a))
    volatile uint8_t *active=(volatile uint8_t *)(uintptr_t)UINT64_C(0xa04204deb);
    volatile uint32_t *handle=(volatile uint32_t *)(uintptr_t)UINT64_C(0xa04204e40);
    if(count!=120u) {fault(s,FX_INTEGRITY);return 0u;}
    if(*active) {
        const Child *current=NATIVE(0x41244a2c,const Child *(*)(void))();
        if(current && current->unit==p->raw_unit_bytes && current->count==p->raw_count &&
           current->total==p->raw_total_bytes)return 1u;
        fault(s,FX_STATE);return 0u;
    }
    /* Preserve the original availability and resource-ownership calls. This
     * entry belongs to Nikon's serialized pipeline builder, never an EL0 call. */
    if(!NATIVE(0x4123cedc,uint32_t (*)(uint32_t))(80u) ||
       !NATIVE(0x4123fdac,uint32_t (*)(uint32_t))(80u))return 0u;
    uint32_t bank=*(volatile uint32_t *)(uintptr_t)UINT64_C(0xa04204cb8);
    if(bank!=2u && bank!=3u) {fault(s,FX_INTEGRITY);return 0u;}
    const uintptr_t *table=*(const uintptr_t *volatile *)(uintptr_t)
        (bank==2u?UINT64_C(0xa04204898):UINT64_C(0xa042048a0));
    const uint32_t *master=table?(const uint32_t *)table[80]:0;
    uintptr_t base=((volatile uintptr_t *)(uintptr_t)UINT64_C(0xa042048b0))[80];
    if(!master || master[1]!=UINT32_C(1086873600) || master[2]!=UINT32_C(9057280) ||
       master[3]!=120u || !base || p->raw_total_bytes>master[1] ||
       NATIVE(0x41247d78,uint32_t (*)(uint32_t))(q[0x39cu/4u])!=1u) {
        fault(s,FX_INTEGRITY);return 0u;
    }
    Child child={base,(uint32_t)p->raw_total_bytes,0u,p->raw_unit_bytes,p->raw_count};
    uint32_t result=NATIVE(0x40ee814c,uint32_t (*)(Child *,uint32_t,volatile uint32_t *))(&child,80u,handle);
    if(result&1u) {*active=1u;return 1u;}
    return 0u;
#undef NATIVE
}
