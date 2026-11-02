#include "fenestra_native_menu.h"
#include "fenestra_native_font.h"
#include "fenestra_memory.h"
#include "lifecycle.h"
#define TEXT 3285u
#define PAGE 0x7000u
#define MAGIC 0x465a634du
typedef struct {
    FxNativeDescriptor descriptor;
    uint32_t magic,installed;
    FxZ63Lifecycle lifecycle;
    _Atomic uint32_t activated;
    FxNativeRow rows[51];uintptr_t events[51];
    FxNativeEvent entry[18],navigation[18];
    FxNativeMenu menu;
    FxNativeTextBank *bank;
} State;
static FxNativePage *setup(void){return (FxNativePage *)(uintptr_t)UINT64_C(0xa02912e10);}
static State *state(void) {
    uintptr_t p=setup()->descriptor;
    if(p==UINT64_C(0xa028d5590) || (p&7u) || p<UINT64_C(0xa00000000) || p>UINT64_C(0xa40000000)-sizeof(State))return 0;
    State *s=(State *)p;return s->magic==MAGIC && s->installed?s:0;
}
static uint32_t noop(void *context){(void)context;return 1u;}
extern void fx_z63_probe_module_start(void);
static uint32_t activate(void *context) {
    (void)context;State *s=state();
    if(!s || !fx_z63_lifecycle_accepts_work(&s->lifecycle))return 1u;
    uint32_t zero=0;
    if(atomic_compare_exchange_strong(&s->activated,&zero,1u))fx_z63_probe_module_start();
    return 1u;
}
static void *allocate(void *context,uint32_t bytes) {
    (void)context;return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u)(bytes,0u);
}
static void release(void *context,void *p) {
    (void)context;((void (*)(void *))(uintptr_t)0x40d32d40u)(p);
}
void fx_z63_menu_install(void) {
    if(state())return;
    FxNativePage *page=setup();const FxNativeDescriptor *d=(const FxNativeDescriptor *)page->descriptor;
    if((page->identity>>16)!=690u || page->descriptor!=UINT64_C(0xa028d5590) ||
       d->count!=50u || d->header!=UINT64_C(0x43e2a5e0) || d->rows!=UINT64_C(0x43e2a6f0) || d->events!=UINT64_C(0x43e2af88))return;
    const FxNativeRow *rows=(const FxNativeRow *)d->rows;
    if(rows[47].word[0]!=912u)return;
    State *s=allocate(0,(uint32_t)sizeof(*s));if(!s)return;
    memset(s,0,sizeof(*s));s->descriptor=*d;
    const FxNativeEvent *original=(const FxNativeEvent *)(uintptr_t)UINT64_C(0x43e5c558);
    /* The original table has 56 records. Keep the checked navigation prefix,
     * then its actual terminal; never scan it with the shared 18-record bound. */
    memcpy(s->navigation,original,14u*sizeof(*original));
    s->navigation[14]=original[55];
    if(s->navigation[14].event!=0x3f2u)return;
    uint32_t select[4]={0x2b3u,0x2b8u,0x2ccu,0x2dbu};
    FxNativeMenuProfile profile={page,&rows[47],s->navigation,0x3f2u,0x2abu,{0x2b3u,0x2b8u,0x2ccu,0x2dbu},0x712u,690u};
    uintptr_t callbacks[5]={(uintptr_t)noop,(uintptr_t)noop,UINT64_C(0x403c32c8),(uintptr_t)noop,(uintptr_t)noop};
    FxNativeEntry entries[]={{TEXT+1u,0x7ff1u,0,0,(uintptr_t)activate,0x712u},
                             {TEXT+2u,0x7ff2u,0,0,(uintptr_t)noop,690u}};
    if(fx_native_menu_build(&s->menu,&profile,PAGE,TEXT,entries,2u,callbacks)!=FX_OK)return;
    memcpy(s->rows+1,rows,50u*sizeof(*rows));
    memcpy(s->events+1,(const void *)d->events,50u*sizeof(uintptr_t));
    s->rows[0]=rows[47];s->rows[0].word[0]=TEXT;s->rows[0].word[2]=65535u;s->rows[0].word[6]=0x7ff0u;
    if(fx_native_menu_link(s->entry,s->navigation,0x3f2u,select,(uintptr_t)noop,PAGE)!=FX_OK)return;
    s->events[0]=(uintptr_t)s->entry;s->descriptor.rows=(uintptr_t)s->rows;s->descriptor.events=(uintptr_t)s->events;s->descriptor.count=51u;
    const uint8_t *fonts=*(const uint8_t *volatile *)(uintptr_t)UINT64_C(0xa0305f980);
    const uintptr_t *attributes=*(const uintptr_t *volatile *)(uintptr_t)UINT64_C(0xa0305f990);
    if(!fonts || !attributes || !attributes[1])return;
    if(fx_native_text_bank_create((FxIndexMemory){0,allocate,release},fonts,attributes,TEXT,29u,TEXT+3u,fonts[912],((const uint8_t *)attributes[1])[912],&s->bank)!=FX_OK)return;
    s->magic=MAGIC;s->installed=1u;fx_z63_lifecycle_record(&s->lifecycle,20u);
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f980)=(uintptr_t)s->bank->fonts;
    *(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f990)=(uintptr_t)s->bank->attributes;
    atomic_store_explicit((_Atomic uintptr_t *)&page->descriptor,(uintptr_t)s,memory_order_release);
}
void fx_z63_menu_lifecycle(uint32_t event) {State *s=state();if(s)fx_z63_lifecycle_record(&s->lifecycle,event);}
FxNativePage *fx_z63_menu_page(uint32_t group,uint32_t id) {
    id&=65535u;State *s=state();
    if(group==2u && id>=PAGE && id<0x9500u)return id==PAGE && s?&s->menu.page:0;
    if(group>6u)return 0;
    uintptr_t root=((volatile uintptr_t *)(uintptr_t)UINT64_C(0xa03420d50))[group];
    return root?(FxNativePage *)(root+(uintptr_t)id*96u):0;
}
void fx_z63_menu_text(int32_t language,int32_t id,const uint16_t **text,uint32_t *bytes) {
    static const uint16_t title[]={'F','e','n','e','s','t','r','a'};
    static const uint16_t run[]={'R','u','n',' ','t','e','s','t',' ','m','o','d','u','l','e'};
    static const uint16_t back[]={'B','a','c','k'};
    if(state()) {
        if(id==(int32_t)TEXT){*text=title;*bytes=sizeof(title);return;}
        if(id==(int32_t)TEXT+1){*text=run;*bytes=sizeof(run);return;}
        if(id==(int32_t)TEXT+2){*text=back;*bytes=sizeof(back);return;}
    }
    int32_t count=*(volatile int32_t *)(uintptr_t)UINT64_C(0xa02822854),index=count>id?id:0;
    const uint32_t *offsets=((const uint32_t *const *)(uintptr_t)(*(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f988)))[language];
    const uint8_t *pool=((const uint8_t *const *)(uintptr_t)(*(volatile uintptr_t *)(uintptr_t)UINT64_C(0xa0305f978)))[language];
    *text=(const uint16_t *)(const void *)(pool+(int32_t)offsets[index]);*bytes=offsets[index+1]-offsets[index];
}
extern uint64_t fx_z63_navigate_original(uint32_t);
uint64_t fx_z63_menu_navigate(uint32_t page) {
    if(page>=PAGE && page<0x9500u)return page==PAGE && state()?((uint64_t (*)(uint32_t,uint32_t))(uintptr_t)0x403be714u)(2u,page):0u;
    return fx_z63_navigate_original(page);
}
