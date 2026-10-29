/* Experimental native C1.10 resolution binding. Kept out of release builds
 * until the same compiled image passes complete native transitions/recording.
 * Only the original menu's stopped/released boundary admits mode selection.
 * Module workers register profiles and observe status; they do not impersonate
 * the UI task or force the capture owner to change a running configuration. */
#include "fenestra_c110_capture_menu.h"
#include "fenestra_capture_service.h"
#include "fenestra_memory.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#define OWNER_STATE (*(volatile uint32_t *)(uintptr_t)UINT64_C(0xa03df96f0))
#define PROP(a) FN(UINT64_C(0x403fb508),uint32_t (*)(uint32_t))(a)
extern uint64_t fx_c110_original_capture_resolution(void *);
extern uint64_t fx_c110_original_capture_lookup(const void *,uint32_t,uint32_t,uint32_t,uint32_t);
extern uint64_t fx_c110_original_capture_initialize(const void *);
extern uint64_t fx_c110_original_capture_restart(const void *);
extern uint64_t fx_c110_original_capture_build(const void *);
extern uint64_t fx_c110_original_capture_job(const void *);
extern uint64_t fx_c110_original_capture_properties(const uint32_t *,const uint32_t *,uint32_t);
extern uint64_t fx_c110_original_capture_property(uint32_t,uint32_t);
extern int fx_c110_menu_ptp_active(void);

static int acquire(FxC110CaptureMenu *s) {
    uint32_t zero=0u;return atomic_compare_exchange_strong(&s->gate,&zero,UINT32_C(0x80000000));
}
static void release(FxC110CaptureMenu *s) {atomic_store_explicit(&s->gate,0u,memory_order_release);}
static int enter(FxC110CaptureMenu *s) {
    uint32_t value=atomic_load(&s->gate);
    for(;;) {
        if(value>=UINT32_C(0x7fffffff))return 0;
        if(atomic_compare_exchange_weak(&s->gate,&value,value+1u))return 1;
    }
}
static void leave(FxC110CaptureMenu *s) {atomic_fetch_sub_explicit(&s->gate,1u,memory_order_release);}
static void fault(FxC110CaptureMenu *s,FxResult error) {
    uint32_t zero=0u;(void)atomic_compare_exchange_strong(&s->hooks.fault,&zero,(uint32_t)error);
    atomic_store(&s->stopping,1u);
}
FxC110CaptureHooks *fx_c110_capture_hooks(void) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();return s?&s->hooks:0;
}
const FxVideoConfigurationMode *fx_c110_capture_mode(const FxC110CaptureMenu *s,uint32_t mode) {
    if(!s || !mode)return 0;
    for(uint32_t i=0;i<s->hooks.configuration->count;++i)
        if(s->hooks.configuration->modes[i].profile.mode_id==mode)return &s->hooks.configuration->modes[i];
    return 0;
}
static uint32_t mode_for_row(const FxC110CaptureMenu *s,uint32_t row) {
    uint32_t i=row-FX_C110_CAPTURE_ROW0;
    return i<s->hooks.configuration->count?s->hooks.profiles[i].mode_id:0u;
}
static uint32_t selected_row(const void *context) {
    if(!context)return UINT32_MAX;
    const uint8_t *p=*(const uint8_t *const *)context;if(!p)return UINT32_MAX;
    const FxC110CaptureDescriptor *d=*(const FxC110CaptureDescriptor *const *)(const void *)(p+0x50u);
    if(!d || d->count>109u+FX_VIDEO_MAX_MODES)return UINT32_MAX;
    uint32_t index=*(const uint16_t *)(const void *)(p+0x32u)+
        (uint32_t)*(const uint16_t *)(const void *)(p+0x58u);
    if(index>=d->count)return UINT32_MAX;
    return ((const FxC110CaptureRow *)(uintptr_t)d->rows)[index].word[6];
}
void fx_c110_capture_menu_attach(void *page) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!s || !page || !atomic_load_explicit(&s->ready,memory_order_acquire) || s->menu_attached)return;
    uintptr_t *slot=(uintptr_t *)((uint8_t *)page+0x50u);
    if(*slot!=UINT64_C(0xa028dcbd0)) {fault(s,FX_INTEGRITY);return;}
    const FxC110CaptureDescriptor *d=(const void *)*slot;
    if(d->count!=109u || d->rows!=UINT64_C(0x43ec393c) || d->events!=UINT64_C(0x43ec4bf8)) {
        fault(s,FX_INTEGRITY);return;
    }
    const FxC110CaptureRow *rows=(const void *)(uintptr_t)d->rows;
    const uint64_t *events=(const void *)(uintptr_t)d->events;
    uintptr_t *callbacks=(uintptr_t *)((uint8_t *)page+0x28u);
    static const uint64_t expected[5]={0x4058ff98u,0x405901c4u,0x403be9b4u,0x405901c8u,0x40590240u};
    if(*callbacks!=UINT64_C(0xa029079a8)) {fault(s,FX_INTEGRITY);return;}
    const uint64_t *original_callbacks=(const void *)*callbacks;
    for(uint32_t i=0;i<5u;++i)
        if(original_callbacks[i]!=expected[i]) {fault(s,FX_INTEGRITY);return;}
    memcpy(s->callbacks,original_callbacks,sizeof(s->callbacks));
    s->callbacks[0]=(uintptr_t)fx_c110_capture_page_init;
    s->descriptor=*d;
    uint32_t added=s->hooks.configuration->count;
    memcpy(s->rows+added,rows,109u*sizeof(*rows));memcpy(s->events+added,events,109u*sizeof(*events));
    for(uint32_t i=0;i<added;++i) {
        const FxVideoConfigurationMode *m=&s->hooks.configuration->modes[i];
        if(rows[m->row_template].word[6]!=m->stock_row) {fault(s,FX_INTEGRITY);return;}
        const FxC110CaptureEvent *source=(const void *)(uintptr_t)events[m->row_template];
        if((source[15].event&65535u)!=0x3fdu) {fault(s,FX_INTEGRITY);return;}
        s->rows[i]=rows[m->row_template];
        s->rows[i].word[0]=FX_C110_CAPTURE_TEXT0+i;
        s->rows[i].word[6]=FX_C110_CAPTURE_ROW0+i;
        memcpy(s->custom_events[i],source,sizeof(s->custom_events[i]));
        for(uint32_t j=0;j<16u;++j)
            if(s->custom_events[i][j].callback==UINT64_C(0x40594434))
                s->custom_events[i][j].callback=(uintptr_t)fx_c110_capture_resolution;
        s->events[i]=(uintptr_t)s->custom_events[i];
    }
    s->descriptor.rows=(uintptr_t)s->rows;s->descriptor.events=(uintptr_t)s->events;
    s->descriptor.count=(uint16_t)(109u+added);
    __asm__ volatile("dmb ish":::"memory");
    *slot=(uintptr_t)&s->descriptor;*callbacks=(uintptr_t)s->callbacks;s->menu_attached=1u;
}
uint64_t fx_c110_capture_page_init(void *page) {
    /* The stock initializer visits its fixed 109-entry table. Run it first,
     * then apply the same native visibility/check/focus APIs to our two rows. */
    uint64_t result=FN(UINT64_C(0x4058ff98),uint64_t (*)(void *))(page);
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!s || !page || !s->menu_attached)return result;
    uint32_t visible=(uint32_t)(fx_c110_menu_ptp_active() &&
        atomic_load_explicit(&s->ready,memory_order_acquire) &&
        !atomic_load(&s->stopping) && !atomic_load(&s->hooks.fault) &&
        PROP(0x06000f06u)==s->hooks.configuration->modes[0].native_codec);
    for(uint32_t i=0;i<s->hooks.configuration->count;++i) {
        uint32_t id=FX_C110_CAPTURE_ROW0+i;
        uint32_t index=FN(UINT64_C(0x403bbdac),uint32_t (*)(void *,uint32_t))(page,id);
        FN(UINT64_C(0x403be640),void (*)(void *,uint32_t,uint32_t))(page,index,!visible);
        FN(UINT64_C(0x403bc630),void (*)(void *,uint32_t,uint32_t))(page,id,0u);
    }
    const FxVideoConfigurationMode *m=fx_c110_capture_mode(s,atomic_load_explicit(&s->hooks.mode,memory_order_acquire));
    if(visible && m && PROP(0x05000f6du)==m->native_family && PROP(0x05000f6eu)==m->native_rate) {
        uint32_t id=FX_C110_CAPTURE_ROW0+(uint32_t)(m-s->hooks.configuration->modes);
        FN(UINT64_C(0x403bc630),void (*)(void *,uint32_t,uint32_t))(page,m->stock_row,0u);
        FN(UINT64_C(0x403bc630),void (*)(void *,uint32_t,uint32_t))(page,id,1u);
        if(FN(UINT64_C(0x403ba6fc),uint32_t (*)(void))()!=1u)
            FN(UINT64_C(0x403bc5c0),void (*)(void *,uint32_t))(page,id);
    }
    return result;
}
uint64_t fx_c110_capture_lookup(const void *table,uint32_t count,uint32_t stride,uint32_t key,uint32_t absent) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(s && atomic_load_explicit(&s->ready,memory_order_acquire) &&
       (uintptr_t)table==UINT64_C(0x42c2da80) && count==109u && stride==24u) {
        uint32_t index=key-FX_C110_CAPTURE_ROW0;
        if(index<s->hooks.configuration->count)key=s->hooks.configuration->modes[index].stock_row;
    }
    return fx_c110_original_capture_lookup(table,count,stride,key,absent);
}
uint64_t fx_c110_capture_resolution(void *context) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!s || !atomic_load_explicit(&s->ready,memory_order_acquire))
        return fx_c110_original_capture_resolution(context);
    uint32_t row=selected_row(context),mode=mode_for_row(s,row);
    const FxVideoConfigurationMode *m=fx_c110_capture_mode(s,mode);
    if(!mode && !atomic_load(&s->hooks.mode))return fx_c110_original_capture_resolution(context);
    if(row==UINT32_MAX || !fx_c110_menu_ptp_active() || atomic_load(&s->stopping) ||
       atomic_load(&s->hooks.fault) || !acquire(s))return 0u;
    /* A stopped-but-not-released owner (4) still owns its old pool. */
    if(OWNER_STATE!=0u || (m && PROP(0x06000f06u)!=m->native_codec)) {
        s->last_result=FX_BUSY;release(s);return 0u;
    }
    uint32_t previous=atomic_load(&s->hooks.mode);
    if(atomic_load(&s->revision)==UINT32_MAX) {fault(s,FX_BUDGET);release(s);return 0u;}
    atomic_store(&s->selecting,1u);
    atomic_store_explicit(&s->hooks.mode,mode,memory_order_release);
    uint64_t result=fx_c110_original_capture_resolution(context);
    atomic_store(&s->selecting,0u);
    if(!result) {
        /* A rejected native property batch is not proof that it was atomic. */
        fault(s,FX_IO);s->last_result=FX_IO;
    } else if(OWNER_STATE!=0u || (m && (PROP(0x06000f06u)!=m->native_codec ||
          PROP(0x05000f6du)!=m->native_family || PROP(0x05000f6eu)!=m->native_rate))) {
        fault(s,FX_INTEGRITY);s->last_result=FX_INTEGRITY;result=0u;
    } else {
        if(previous!=mode)atomic_fetch_add(&s->revision,1u);
        s->last_result=FX_OK;
    }
    release(s);return result;
}
static int capture_property(uint32_t key) {
    return key==0x06000f06u || key==0x05000f6du || key==0x05000f6eu;
}
uint64_t fx_c110_capture_property(uint32_t key,uint32_t value) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!s || !atomic_load_explicit(&s->ready,memory_order_acquire) ||
       !atomic_load(&s->hooks.mode) || !capture_property(key))
        return fx_c110_original_capture_property(key,value);
    /* Native codec menu callbacks use this scalar entry, which does not call
     * the batch entry. Restore before its native validation/publication and
     * notifications, while the old capture owner has released its resources. */
    if(atomic_load(&s->stopping) || !acquire(s))return 0u;
    if(OWNER_STATE!=0u) {release(s);return 0u;}
    if(atomic_load(&s->revision)==UINT32_MAX) {fault(s,FX_BUDGET);release(s);return 0u;}
    atomic_store_explicit(&s->hooks.mode,0u,memory_order_release);
    uint64_t result=fx_c110_original_capture_property(key,value);
    if(!result)fault(s,FX_IO);
    else atomic_fetch_add(&s->revision,1u);
    release(s);return result;
}
uint64_t fx_c110_capture_properties(const uint32_t *keys,const uint32_t *values,uint32_t count) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    uint32_t mode=s?atomic_load(&s->hooks.mode):0u;
    if(!s || !atomic_load_explicit(&s->ready,memory_order_acquire) || !mode || !keys || !values)
        return fx_c110_original_capture_properties(keys,values,count);
    const FxVideoConfigurationMode *m=fx_c110_capture_mode(s,mode);
    int changes=0;
    for(uint32_t i=0;i<count;++i)
        if(capture_property(keys[i]))changes=1;
    if(!changes)return fx_c110_original_capture_properties(keys,values,count);
    /* The original selector's own two-property batch is already inside our
     * exclusive transaction. No unrelated batch inherits this exemption. */
    if(atomic_load(&s->selecting) && atomic_load(&s->gate)==UINT32_C(0x80000000) &&
       count==2u && keys[0]==0x05000f6du && keys[1]==0x05000f6eu && m && values[0]==m->native_family &&
       values[1]==m->native_rate)
        return fx_c110_original_capture_properties(keys,values,count);
    if(atomic_load(&s->stopping) || !acquire(s))return 0u;
    if(OWNER_STATE!=0u) {release(s);return 0u;}
    if(atomic_load(&s->revision)==UINT32_MAX) {fault(s,FX_BUDGET);release(s);return 0u;}
    /* Restore BEFORE property notifications build new capture descriptors,
     * rather than waiting until their later owner-initialize call. */
    atomic_store_explicit(&s->hooks.mode,0u,memory_order_release);
    uint64_t result=fx_c110_original_capture_properties(keys,values,count);
    if(!result)fault(s,FX_IO);
    else atomic_fetch_add(&s->revision,1u);
    release(s);return result;
}
static uint64_t admit(const void *argument,uint64_t (*original)(const void *)) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!s || !atomic_load_explicit(&s->ready,memory_order_acquire))return original(argument);
    if(atomic_load(&s->stopping) || atomic_load(&s->hooks.fault) || !enter(s))return 0u;
    uint32_t mode=atomic_load(&s->hooks.mode);
    /* Other native format selections may bypass the resolution page. Restore
     * before a new owner starts, never clear dimensions on an active pool. */
    if(mode && OWNER_STATE==0u) {
        const FxVideoConfigurationMode *m=fx_c110_capture_mode(s,mode);
        if(!m || PROP(0x06000f06u)!=m->native_codec || PROP(0x05000f6du)!=m->native_family || PROP(0x05000f6eu)!=m->native_rate) {
            atomic_store(&s->hooks.mode,0u);atomic_fetch_add(&s->revision,1u);
        }
    }
    uint64_t result=original(argument);leave(s);return result;
}
#define ADMIT(name) uint64_t fx_c110_capture_##name(const void *p) {return admit(p,fx_c110_original_capture_##name);}
uint64_t fx_c110_capture_initialize(const void *p) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    int rebuilding=s && atomic_load(&s->quick.reinitialize);
    if(rebuilding && (!p || OWNER_STATE!=0u || atomic_load(&s->quick.pending) ||
       atomic_load(&s->stopping) || atomic_load(&s->hooks.fault))) {
        fault(s,FX_INTEGRITY);return 0u;
    }
    uint64_t result=admit(p,fx_c110_original_capture_initialize);
    if(rebuilding) {
        if(result&1u)atomic_store_explicit(&s->quick.reinitialize,0u,memory_order_release);
        else fault(s,FX_IO);
    }
    return result;
}
uint64_t fx_c110_capture_restart(const void *p) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(s && atomic_load(&s->quick.reinitialize)) {
        if(!p || OWNER_STATE!=0u || atomic_load(&s->quick.pending) ||
           atomic_load(&s->stopping) || atomic_load(&s->hooks.fault)) {
            fault(s,FX_INTEGRITY);return 0u;
        }
        /* The normal quick route asks to restart an owner in state4. Our
         * transaction released that owner before projecting the new context.
         * Preserve its native operation and use the original fresh-owner ABI. */
        const uint64_t arguments[2]={s->quick.operation,*(const uint64_t *)p};
        atomic_store(&s->quick.reinitialize,0u);
        uint64_t result=admit(arguments,fx_c110_original_capture_initialize);
        if(!(result&1u))fault(s,FX_IO);
        return result;
    }
    if(s && atomic_load(&s->quick.pending) && OWNER_STATE==4u) {
        /* A context constructed before the released-mode handoff is not
         * safe to admit. Never repair it by changing a live pool's fields. */
        fault(s,FX_INTEGRITY);return 0u;
    }
    return admit(p,fx_c110_original_capture_restart);
}
ADMIT(build)
ADMIT(job)

FxResult fx_c110_capture_resource(void *context,uint32_t op,const FxNamedBinding *binding,FxPlatformOwner *owner,
    uint64_t token,uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)binding;(void)token;
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!s && op==FX_RESOURCE_ATTACH)s=fx_c110_capture_create();
    if(!s)return FX_CAPABILITY;
    if(op==FX_RESOURCE_ATTACH){*cookie=(uintptr_t)s;return FX_OK;}
    if(*cookie!=(uintptr_t)s)return FX_STALE;
    uintptr_t current=atomic_load(&s->owner);
    if(op==FX_RESOURCE_PANIC || op==FX_RESOURCE_DETACH) {
        if(current==(uintptr_t)owner) {
            fx_c110_capture_stop();
            /* A committed callback's data stays pinned until reboot, even if
             * its module finishes. No hot unload or shared-owner shutdown. */
            if(atomic_load(&s->ready)) {
                if(op==FX_RESOURCE_PANIC)return FX_OK;
                if(!fx_c110_capture_stopped())return FX_BUSY;
                /* Normal power-off needs an acknowledged stop, not a hot
                 * unload. Original teardown may still read the copied mode
                 * records; keep all callbacks/data and the owner pinned. */
                *cookie=0;return FX_OK;
            }
            if(!acquire(s))return FX_BUSY;
            atomic_store(&s->owner,0u);s->uploaded=0;release(s);
        }
        if(op==FX_RESOURCE_DETACH)*cookie=0;
        return FX_OK;
    }
    if(op!=FX_RESOURCE_CALL || !q || !a)return FX_ARGUMENT;
    uint64_t action=q->arg[2];
    if(action==FX_VIDEO_QUERY) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        FxVideoConfigurationInfo info={1u,sizeof(info),(1u<<FX_VIDEO_LEGACY_ABI)|(1u<<FX_VIDEO_CONFIGURATION_ABI),
            sizeof(FxVideoConfigurationData),sizeof(FxVideoConfigurationMode),FX_VIDEO_MAX_MODES,
            1u<<FX_CAPTURE_CODEC_R3D,0u};
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);
        /* Keep legacy query fields: old modules can still upload ABI3 pairs. */
        a->value[0]=FX_VIDEO_LEGACY_ABI;a->value[1]=FX_CAPTURE_REQUIRED|FX_CAPTURE_NATIVE_MENU;
        a->value[2]=sizeof(FxVideoConfiguration);a->value[3]=FX_VIDEO_MAX_MODES;return FX_OK;
    }
    if(action==FX_VIDEO_STATUS) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        if(current!=(uintptr_t)owner || !atomic_load(&s->ready))return FX_STALE;
        if(atomic_load(&s->hooks.fault))return FX_INTEGRITY;
        if(!fx_c110_menu_ptp_active() || atomic_load(&s->stopping) || !enter(s))return FX_BUSY;
        a->value[0]=atomic_load(&s->revision);a->value[1]=atomic_load(&s->hooks.mode);
        a->value[2]=OWNER_STATE!=0u;leave(s);return FX_OK;
    }
    if(!fx_c110_menu_ptp_active() || atomic_load(&s->stopping))return FX_STATE;
    if(!acquire(s))return FX_BUSY;
    FxResult result=FX_ARGUMENT;
    if(action==FX_VIDEO_BEGIN) {
        uint64_t bytes=q->arg[3];
        if(!q->bytes && bytes>=fx_video_configuration_bytes(1u) &&
           bytes<=fx_video_configuration_bytes(FX_VIDEO_MAX_MODES) &&
           (bytes-sizeof(FxVideoConfigurationData))%sizeof(FxVideoConfigurationMode)==0u) {
            if(atomic_load(&s->ready) || (current && current!=(uintptr_t)owner))result=FX_BUSY;
            else {
                if(!s->hooks.configuration || s->configuration_capacity<bytes) {
                    FxVideoConfigurationData *configuration=FN(UINT64_C(0x40f41084),void *(*)(uint32_t,uint32_t))((uint32_t)bytes,0u);
                    if(!configuration){release(s);return FX_BUDGET;}
                    fx_allocation_observe(s->allocation_observer,(uint32_t)bytes,FX_ALLOC_REQUESTED);
                    if(s->hooks.configuration) {
                        FN(UINT64_C(0x40f410c4),void (*)(void *))(s->hooks.configuration);
                        if(s->configuration_accounted)
                            fx_allocation_observe(s->allocation_observer,s->configuration_accounted,FX_ALLOC_FREE_PUBLISHED);
                    }
                    s->hooks.configuration=configuration;s->configuration_capacity=(uint32_t)bytes;
                    s->configuration_accounted=s->allocation_observer.event?(uint32_t)bytes:0u;
                    fx_allocation_observe(s->allocation_observer,(uint32_t)bytes,FX_ALLOC_PUBLISHED);
                }
                if(!s->hooks.configuration)result=FX_BUDGET;
                else {
                    FxVideoConfigurationData *configuration=s->hooks.configuration;
                    memset(&s->hooks,0,sizeof(s->hooks));s->hooks.configuration=configuration;
                    memset(&s->quick,0,sizeof(s->quick));
                    atomic_store(&s->owner,(uintptr_t)owner);s->uploaded=0u;s->upload_bytes=(uint32_t)bytes;result=FX_OK;
                }
            }
        }
    } else if(current!=(uintptr_t)owner || atomic_load(&s->ready))result=FX_STALE;
    else if(action==FX_VIDEO_APPEND) {
        if(q->bytes && q->arg[3]==s->uploaded && s->uploaded<=s->upload_bytes && q->bytes<=s->upload_bytes-s->uploaded) {
            memcpy((uint8_t *)s->hooks.configuration+s->uploaded,q->data,q->bytes);
            s->uploaded+=q->bytes;a->value[0]=s->uploaded;result=FX_OK;
        }
    } else if(action==FX_VIDEO_COMMIT && !q->bytes && !q->arg[3] && s->upload_bytes && s->uploaded==s->upload_bytes) {
        result=fx_c110_capture_hooks_prepare(&s->hooks,s->hooks.configuration,s->upload_bytes);
        if(result==FX_OK && !fx_c110_capture_quick_prepare())result=FX_INTEGRITY;
        if(result==FX_OK) {
            for(uint32_t i=0;i<s->hooks.configuration->count;++i)
                for(uint32_t j=0;j<48u;++j)s->labels[i][j]=(uint16_t)(uint8_t)s->hooks.configuration->modes[i].label[j];
            s->hooks.handoff=fx_c110_capture_quick_handoff;s->hooks.display_mode=fx_c110_capture_quick_display_mode;
            atomic_store_explicit(&s->hooks.prepared,1u,memory_order_release);
            atomic_store_explicit(&s->ready,1u,memory_order_release);a->value[0]=1u;
        }
    }
    release(s);return result;
}
void fx_c110_capture_stop(void) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();if(s && atomic_load(&s->ready)) {
        atomic_store(&s->stopping,1u);atomic_store(&s->quick.pending,0u);
    }
}
int fx_c110_capture_stopped(void) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    return !s || !atomic_load(&s->ready) || !atomic_load(&s->hooks.mode) || atomic_load(&s->stop_ack)!=0u;
}
uint32_t fx_c110_capture_poll(void) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();if(!s || !atomic_load(&s->ready))return 0u;
    uint32_t error=atomic_load(&s->hooks.fault);
    if(error)atomic_store(&s->stopping,1u);
    if(atomic_load(&s->stopping) && !atomic_load(&s->stop_ack) && acquire(s)) {
        uint32_t state=OWNER_STATE;
        if(!atomic_load(&s->hooks.mode) || state==0u || state==4u)atomic_store(&s->stop_ack,1u);
        else (void)FN(UINT64_C(0x41091700),uint32_t (*)(void))();
        /* Stop request is asynchronous. Never release a pool or clear mode
         * until original completion; uncertain resources remain owned. */
        release(s);
    }
    return error;
}
