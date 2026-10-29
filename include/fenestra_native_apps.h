#ifndef FENESTRA_NATIVE_APPS_H
#define FENESTRA_NATIVE_APPS_H
#include "fenestra_native_menu.h"
#include "fenestra_native_namespace.h"
#define FX_NATIVE_APP_ROWS 10u
#define FX_NATIVE_APP_PAGE_COUNT ((FX_NATIVE_UI_OWNERS+FX_NATIVE_APP_ROWS-1u)/FX_NATIVE_APP_ROWS)
typedef struct FxNativeAppLink FxNativeAppLink;
/* Creator-owned per-view link cache; never supplied in a module message. */
typedef struct {uint32_t text,value,page,enabled;FxNativeAppLink **links;FxAllocationObserver observer;} FxNativeAppEntry;
/* Core-owned GUI records. The root is inline so list recovery never needs
 * its first heap allocation after modules have consumed the native heap. */
typedef struct FxNativeAppFrame {
    struct FxNativeAppFrame *next;FxNativeMenu native;
    uint32_t ordinal,count,pinned,current,more,notice,prepared,indices[FX_NATIVE_APP_ROWS];
    FxNativeAppEntry entries[FX_NATIVE_APP_ROWS];
    _Atomic uint32_t accounted;
    uint32_t pending_error,failed_selection,failed_instance,failed_page;
} FxNativeAppFrame;
typedef struct {
    FxNativeMenuProfile profile;uintptr_t callbacks[5];
    void *context;void *(*allocate)(uint32_t);
    uint32_t (*count)(void *);
    FxResult (*entry)(void *,uint32_t,const FxNativePage *,int,FxNativeAppEntry *);
    FxResult (*action)(void *,uint32_t);
    _Atomic(FxNativeAppFrame *) frames;FxNativeAppLink *links;
    uint32_t title,previous_text,next_text,empty_text,back_text,unavailable_text,retry_text;
    uint32_t error;
    FxNativeAppFrame *current[FX_NATIVE_APP_PAGE_COUNT];
    FxRetainedAccount account;_Atomic uint32_t accounted;
    uint32_t embedded; /* Enclosing controller allocation already owns these bytes. */
    FxNativeAppFrame root;
    _Atomic uint32_t navigation_pending;
    uint32_t navigation_source,navigation_instance,navigation_page;
    void (*capture_task)(void); /* Trusted original page-open task, before input. */
} FxNativeApps;
void fx_native_apps_account_retained(FxNativeApps *);
FxResult fx_native_apps_init(FxNativeApps *,const FxNativeMenuProfile *,const uintptr_t callbacks[5],
    void *context,void *(*allocate)(uint32_t),uint32_t (*count)(void *),
    FxResult (*entry)(void *,uint32_t,const FxNativePage *,int,FxNativeAppEntry *),
    FxResult (*action)(void *,uint32_t),uint32_t title,uint32_t previous,uint32_t next,uint32_t empty,uint32_t back,
    uint32_t unavailable,uint32_t retry);
/* Original GUI task only; snapshots/links/native storage are retained.
 * entry(parent=NULL,selected=0) probes without allocating a native UI;
 * preparing a row copies loader identity, selected=1 prepares its actual page.
 * BUDGET while rebuilding a closed frame presents an allocation-free notice
 * in that retained frame; Retry follows the normal native close/reopen path.
 * Root storage is inline; Next targets are reserved before publishing a row. */
FxNativePage *fx_native_apps_page(FxNativeApps *,uint32_t page);
uint32_t fx_native_apps_value(FxNativeApps *,const FxNativePage *,uint32_t row,int *enabled);
FxNativeApps *fx_native_apps_domain(void);
uint32_t fx_native_apps_condition(const FxNativePage *,const FxNativeEvent *);
/* Native UI task, under GUI exclusion; navigate the prepared destination only
 * after unlocking. A failed target reopens the retained source as a notice. */
FxResult fx_native_apps_prepare_navigation(FxNativeApps *,const FxNativePage *,uint32_t *destination);
FxResult fx_native_apps_flush_native(void); /* Independently bound by each profile. */
static inline int fx_native_apps_pending(const FxNativeApps *apps) {
    return apps && atomic_load_explicit(&apps->navigation_pending,memory_order_acquire);
}
static inline uint32_t fx_native_apps_page_id(uint32_t ordinal) {
    return ordinal?FX_NATIVE_APPLICATION_PAGE_BASE+ordinal-1u:FX_NATIVE_WONDER_PAGE;
}
static inline int fx_native_apps_is_page(uint32_t id) {
    return id==FX_NATIVE_WONDER_PAGE || (id>=FX_NATIVE_APPLICATION_PAGE_BASE &&
        id<FX_NATIVE_APPLICATION_PAGE_BASE+FX_NATIVE_APP_PAGE_COUNT-1u);
}
#endif
