/* C1.10 native shooting quick selector. Module profiles remain the only
 * source of custom geometry. This layer adapts Nikon's RAM UI descriptions
 * and stages a transaction; it never changes a running capture allocation. */
#include "fenestra_c110_capture_menu.h"
#include "fenestra_memory.h"
#define FN(a,t) ((t)(uintptr_t)(a))
#define U32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define U16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define VIEW (*(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa03546148))
#define OWNER U32(UINT64_C(0xa03df96f0))
#define PROP(k) FN(UINT64_C(0x403fb508),uint32_t (*)(uint32_t))(k)
#define ORIGINAL_VIEW UINT64_C(0xa02871398)
#define CONTROL UINT64_C(0xa03546350)
#define CURSOR UINT64_C(0xa035462d8)
extern uint64_t fx_c110_original_capture_quick_template(uint32_t,uint32_t);
extern uint64_t fx_c110_original_capture_quick_control(uint32_t);
extern uint64_t fx_c110_original_capture_quick_initialize(uint32_t);
extern uint64_t fx_c110_original_capture_quick_current(void);
extern uint64_t fx_c110_original_capture_quick_commit(void);
extern uint64_t fx_c110_original_capture_deferred(const uint32_t *,const uint32_t *,uint32_t);
extern int fx_c110_menu_ptp_active(void);

static int lock(FxC110CaptureMenu *s) {
    uint32_t zero=0u;return atomic_compare_exchange_strong(&s->gate,&zero,UINT32_C(0x80000000));
}
static void unlock(FxC110CaptureMenu *s) {atomic_store_explicit(&s->gate,0u,memory_order_release);}
static void fault(FxC110CaptureMenu *s,FxResult error) {
    uint32_t zero=0u;(void)atomic_compare_exchange_strong(&s->hooks.fault,&zero,(uint32_t)error);
    atomic_store(&s->stopping,1u);
}
static int active(const FxC110CaptureMenu *s) {
    return s && atomic_load_explicit(&s->ready,memory_order_acquire) &&
        fx_c110_menu_ptp_active() && !atomic_load(&s->stopping) && !atomic_load(&s->hooks.fault);
}
static uint32_t added(const FxC110CaptureMenu *s);
static int ours(const FxC110CaptureMenu *s) {
    return active(s) && s->quick.prepared && VIEW==(uintptr_t)&s->quick.control &&
        U32(CONTROL)==0xd2u && U16(CONTROL+4u)==17u+added(s);
}
static uint32_t added(const FxC110CaptureMenu *s) {return s->hooks.configuration->count;}
static uint32_t stock(const FxC110CaptureMenu *s,uint32_t index) {
    return index<added(s)?s->hooks.configuration->modes[index].quick_template:index-added(s);
}
static uint32_t visible(const FxC110CaptureMenu *s,uint32_t index) {
    uint32_t mode=atomic_load_explicit(&s->hooks.mode,memory_order_acquire);
    for(uint32_t i=0;i<added(s);++i)
        if(mode==s->hooks.profiles[i].mode_id && index==s->hooks.configuration->modes[i].quick_template)return i;
    return index+added(s);
}
int fx_c110_capture_quick_prepare(void) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();if(!s)return 0;
    FxC110CaptureQuick *q=&s->quick;if(q->prepared)return 1;
    const FxC110QuickControl *control=(const void *)(uintptr_t)ORIGINAL_VIEW;
    if(control->id!=0xd2u || control->kind!=2u || control->count!=63u ||
       control->entries!=UINT64_C(0xa0286a408))return 0;
    q->control=*control;q->control.entries=(uintptr_t)q->entries;
    memcpy(q->entries,(const void *)(uintptr_t)control->entries,sizeof(q->entries));
    const FxC110QuickVariant *original=(const void *)(uintptr_t)q->entries[3].data;
    if(q->entries[0].flags!=1u || q->entries[3].flags!=2u ||
       original[0].word[1]!=UINT64_C(0xa0289c2a0) ||
       original[1].word[1]!=UINT64_C(0xa0289c658))return 0;
    for(uint32_t variant=0;variant<2u;++variant) {
        const FxC110QuickWidget *widget=(const void *)(uintptr_t)original[variant].word[1];
        if(widget->count!=17u || widget->data!=(variant?UINT64_C(0xa0289cba8):UINT64_C(0xa0289ca10)))return 0;
        const FxC110QuickValue *values=(const void *)(uintptr_t)widget->data;
        for(uint32_t i=0;i<17u+added(s);++i) {
            uint32_t source=stock(s,i);
            q->icons[variant][i]=values[source];q->icons[variant][i].index=i;
            q->variants[i][variant]=original[variant];
            q->widgets[i][variant]=*widget;q->widgets[i][variant].id=0x4cc9u+i;
            q->widgets[i][variant].data=(uintptr_t)q->icons[variant];
            q->widgets[i][variant].count=17u+added(s);
            for(uint32_t cell=0;cell<4u;++cell)
                q->widgets[i][variant].cells[cell]=(uintptr_t)&q->cells[i][variant][cell];
            q->variants[i][variant].word[1]=(uintptr_t)&q->widgets[i][variant];
        }
    }
    for(uint32_t i=0;i<17u+added(s);++i)q->entries[3u+i]=(FxC110QuickEntry){(uintptr_t)q->variants[i],2u};
    q->caption_variant=*(const FxC110QuickVariant *)(uintptr_t)q->entries[0].data;
    if(q->caption_variant.word[0]!=UINT64_C(0xa0289c0c0))return 0;
    q->caption=*(const FxC110QuickWidget *)(uintptr_t)q->caption_variant.word[0];
    if(q->caption.count!=17u || q->caption.data!=UINT64_C(0xa0289c108))return 0;
    const FxC110QuickValue *captions=(const void *)(uintptr_t)q->caption.data;
    for(uint32_t i=0;i<17u+added(s);++i) {
        q->captions[i]=captions[stock(s,i)];q->captions[i].index=i;
        if(i<added(s))q->captions[i].resource=FX_C110_CAPTURE_TEXT0+i;
    }
    q->caption.data=(uintptr_t)q->captions;q->caption.count=17u+added(s);
    for(uint32_t i=0;i<4u;++i)q->caption.cells[i]=(uintptr_t)&q->caption_cells[i];
    q->caption_variant.word[0]=(uintptr_t)&q->caption;
    q->entries[0].data=(uintptr_t)&q->caption_variant;
    q->prepared=1u;return 1;
}
uint64_t fx_c110_capture_quick_template(uint32_t layout,uint32_t id) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    uint64_t result=fx_c110_original_capture_quick_template(layout,id);
    if(!active(s) || id!=0xd2u || (layout!=1u && layout!=2u && layout!=4u) || PROP(0x06000f06u)!=s->hooks.configuration->modes[0].native_codec)
        return result;
    if(!s->quick.prepared || (VIEW!=ORIGINAL_VIEW && VIEW!=(uintptr_t)&s->quick.control)) {
        fault(s,FX_INTEGRITY);return result;
    }
    return FN(UINT64_C(0x403e876c),uint64_t (*)(uint32_t,uint32_t))(5u,1688u);
}
uint64_t fx_c110_capture_quick_control(uint32_t id) {
    uint64_t result=fx_c110_original_capture_quick_control(id);
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!active(s) || id!=0xd2u || PROP(0x06000f06u)!=s->hooks.configuration->modes[0].native_codec)return result;
    if(!s->quick.prepared || VIEW!=ORIGINAL_VIEW) {fault(s,FX_INTEGRITY);return result;}
    /* The original selector replaces these pointers after template lookup.
     * Bind at its completed control selection, before native UI construction. */
    __asm__ volatile("dmb ish":::"memory");
    VIEW=(uintptr_t)&s->quick.control;
    volatile uintptr_t *base=(void *)(uintptr_t)UINT64_C(0xa03546140);
    volatile uintptr_t *alternate=(void *)(uintptr_t)UINT64_C(0xa03546150);
    if(*base==ORIGINAL_VIEW)*base=(uintptr_t)&s->quick.control;
    if(*alternate==ORIGINAL_VIEW)*alternate=(uintptr_t)&s->quick.control;
    return result;
}
uint64_t fx_c110_capture_quick_initialize(uint32_t id) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    int enabled=active(s) && id==0xd2u && VIEW==(uintptr_t)&s->quick.control;
    if(enabled)s->quick.initializing=1u;
    uint64_t result=fx_c110_original_capture_quick_initialize(id);
    if(!enabled)return result;
    s->quick.initializing=0u;
    if(U32(CONTROL)!=id || U16(CONTROL+4u)!=17u) {fault(s,FX_INTEGRITY);return result;}
    uint32_t index=(uint32_t)fx_c110_original_capture_quick_current();
    if(index>=17u) {fault(s,FX_INTEGRITY);return result;}
    uint32_t selection=visible(s,index);
    for(uint32_t i=0;i<17u+added(s);++i) {U32(CONTROL+8u+i*8u)=i;U32(CONTROL+12u+i*8u)=i;}
    U16(CONTROL+4u)=(uint16_t)(17u+added(s));
    U16(UINT64_C(0xa03546348))=(uint16_t)(17u+added(s));U16(UINT64_C(0xa0354634a))=0u;U16(UINT64_C(0xa0354634c))=0u;
    U16(CURSOR)=(uint16_t)selection;U16(CURSOR+2u)=(uint16_t)selection;
    U16(UINT64_C(0xa035462e8))=(uint16_t)selection;
    U32(CONTROL+0x228u)=selection;U16(CONTROL+0x22cu)=(uint16_t)selection;
    return result;
}
uint64_t fx_c110_capture_quick_current(void) {
    uint64_t result=fx_c110_original_capture_quick_current();
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(ours(s) && !s->quick.initializing && result<17u)return visible(s,(uint32_t)result);
    return result;
}
static int parameter(uint32_t key) {return key==0x06000f06u || key==0x05000f6du || key==0x05000f6eu;}
int fx_c110_capture_quick_display_mode(uint32_t *mode) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!mode || !active(s) || !atomic_load_explicit(&s->quick.pending,memory_order_acquire))return 0;
    uint32_t desired=atomic_load_explicit(&s->quick.desired_mode,memory_order_acquire);
    if(!atomic_load_explicit(&s->quick.pending,memory_order_acquire))return 0;
    *mode=desired;return 1;
}
static void begin(FxC110CaptureMenu *s,uint32_t mode) {
    FxC110CaptureQuick *q=&s->quick;
    q->previous_mode=atomic_load(&s->hooks.mode);q->desired_mode=mode;q->revision=atomic_load(&s->revision);
    q->codec=PROP(0x06000f06u);q->family=PROP(0x05000f6du);q->rate=PROP(0x05000f6eu);
}
uint64_t fx_c110_capture_quick_commit(void) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!ours(s))return fx_c110_original_capture_quick_commit();
    FxC110CaptureQuick *q=&s->quick;
    if(atomic_load(&q->reinitialize) || !lock(s))return 0u;
    /* Nikon applies horizontal highlights to one deferred batch before the
     * selector closes. A later highlight must replace the pending request;
     * otherwise the UI advances while the first hovered mode remains armed.
     * Handoff uses this same gate, and reinitialize prohibits editing a
     * released owner. Keep an existing pending epoch even when the user moves
     * back to its original mode: the native deferred batch still must drain. */
    uint32_t index=U16(CURSOR);
    if(index>=17u+added(s)) {fault(s,FX_INTEGRITY);unlock(s);return 0u;}
    uint32_t target=stock(s,index),mode=index<added(s)?s->hooks.profiles[index].mode_id:0u;
    begin(s,mode);q->selecting=1u;q->staged=0u;
    U16(CURSOR)=(uint16_t)target;
    uint64_t result=fx_c110_original_capture_quick_commit();
    U16(CURSOR)=(uint16_t)index;q->selecting=0u;
    if(!q->staged)fault(s,FX_IO);
    else if(q->previous_mode!=mode)atomic_store_explicit(&q->pending,1u,memory_order_release);
    unlock(s);return result;
}
uint64_t fx_c110_capture_deferred(const uint32_t *keys,const uint32_t *values,uint32_t count) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!active(s) || !keys || !values)return fx_c110_original_capture_deferred(keys,values,count);
    int changes=0;
    for(uint32_t i=0;i<count;++i)if(parameter(keys[i]))changes=1;
    if(!changes)return fx_c110_original_capture_deferred(keys,values,count);
    FxC110CaptureQuick *q=&s->quick;
    int nested=q->selecting && atomic_load(&s->gate)==UINT32_C(0x80000000);
    if(!nested && !atomic_load(&s->hooks.mode))return fx_c110_original_capture_deferred(keys,values,count);
    if(!nested && !lock(s))return 0u;
    if(!nested && !atomic_load(&q->pending))begin(s,0u);
    for(uint32_t i=0;i<count;++i) {
        if(keys[i]==0x06000f06u)q->codec=values[i];
        if(keys[i]==0x05000f6du)q->family=values[i];
        if(keys[i]==0x05000f6eu)q->rate=values[i];
    }
    uint64_t result=fx_c110_original_capture_deferred(keys,values,count);
    if(!(result&1u))fault(s,FX_IO);
    else if(nested)q->staged=1u;
    else atomic_store_explicit(&q->pending,1u,memory_order_release);
    if(!nested)unlock(s);
    return result;
}
int fx_c110_capture_quick_handoff(void) {
    FxC110CaptureMenu *s=fx_c110_capture_menu();
    if(!s || !atomic_load_explicit(&s->quick.pending,memory_order_acquire))return 1;
    FxC110CaptureQuick *q=&s->quick;
    uint32_t state=OWNER;
    if(state!=0u && state!=4u)return 1;
    if(!active(s))return 0;
    /* A native reader or a release-time parameter callback may already own
     * the gate. Keep its committed geometry and leave this epoch pending;
     * ordinary contention is neither an ownership error nor a reason to
     * recursively invoke Nikon's release routine. */
    if(!lock(s))return 1;
    if(q->revision!=atomic_load(&s->revision) || q->previous_mode!=atomic_load(&s->hooks.mode) ||
       q->revision==UINT32_MAX || PROP(0x06000f06u)!=q->codec ||
       (q->codec==s->hooks.configuration->modes[0].native_codec && (PROP(0x05000f6du)!=q->family || PROP(0x05000f6eu)!=q->rate))) {
        fault(s,FX_INTEGRITY);unlock(s);return 0;
    }
    if(state==4u) {
        uint32_t operation=U32(UINT64_C(0xa03df96f8));
        if(!((operation>=5u && operation<=12u) || (operation>=15u && operation<=20u)) ||
           !(FN(UINT64_C(0x41091b40),uint32_t (*)(void))()&1u) || OWNER!=0u) {
            fault(s,FX_INTEGRITY);unlock(s);return 0;
        }
        q->operation=operation;atomic_store(&q->reinitialize,1u);
    }
    if(OWNER!=0u) {fault(s,FX_INTEGRITY);unlock(s);return 0;}
    atomic_store_explicit(&s->hooks.mode,q->desired_mode,memory_order_release);
    if(q->previous_mode!=q->desired_mode)atomic_fetch_add(&s->revision,1u);
    atomic_store_explicit(&q->pending,0u,memory_order_release);
    unlock(s);return 1;
}
