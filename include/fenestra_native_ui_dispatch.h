#ifndef FENESTRA_NATIVE_UI_DISPATCH_H
#define FENESTRA_NATIVE_UI_DISPATCH_H
#include "fenestra_native_ui.h"
#include "fenestra_tasks.h"
/* Copied UI work is committed by the original input task, never a worker.
 * Owners and native page/callback storage stay alive until reboot. */
#define FX_UI_DISPATCH_BATCH 8u
typedef struct {
    _Atomic uintptr_t task,last_task,owners,ready,pages;
    uintptr_t backlog; /* Sole original UI consumer; bounded work per poll. */
    _Atomic uint32_t pending,running;
    _Atomic uint64_t submitted,completed;
    FxTaskDomain tasks;
    /* Native-ID high-byte buckets. Disjoint ranges, with one intrusive link
     * per intersected bucket; no allocation in bind/find. A one-page range
     * can occupy one of at most 256 disjoint IDs in a bucket. */
    _Atomic uintptr_t page_routes[256];_Atomic uint32_t page_writer;
    FxTextRegions *text_regions;
} FxNativeUiDispatch;
typedef struct {
    uintptr_t current_page,poll,next_wait;
    uint32_t interval_ms,reserved;
    uintptr_t event_group,event_set;
    uint32_t wake_mask,padding;
} FxNativeUiDispatchProfile;
extern const FxNativeUiDispatchProfile fx_native_ui_dispatch_profile;
FxNativeUiDispatch *fx_native_ui_dispatch_domain(void);
FxResult fx_native_ui_flush_owner(FxPlatformOwner *);
/* Creator-only, before publication/worker start. A registered owner, page or
 * dispatcher cannot be reset/reused/freed while native references exist. */
FxResult fx_ui_dispatch_register(FxNativeUiDispatch *,FxPlatformOwner *);
FxResult fx_ui_dispatch_bind_page(FxNativeUiDispatch *,FxNativeUi *);
/* Normal core adapter only, after bind_page. Span and links are caller-owned
 * trusted storage, retained through reboot; never module memory. count may
 * cross any number of high-byte buckets. Publication is all-or-nothing;
 * no mutation/rebind/reuse/unregister operation exists. logical is the first
 * logical index, independently of the 16-bit native identity. */
uint32_t fx_ui_dispatch_span_links(uint32_t first,uint32_t count);
FxResult fx_ui_dispatch_bind_span(FxNativeUiDispatch *,FxNativeUi *,FxUiPageSpan *,FxUiPageRoute *,
    uint32_t links,uint32_t first,uint32_t count,uint32_t logical);
FxNativeUi *fx_ui_dispatch_resolve_page(const FxNativeUiDispatch *,uint32_t page,uint32_t *logical);
FxNativeUi *fx_ui_dispatch_find_page(const FxNativeUiDispatch *,uint32_t page);
FxResult fx_ui_dispatch_request(FxNativeUiDispatch *,FxPlatformOwner *);
void fx_ui_dispatch_capture(FxNativeUiDispatch *,uintptr_t task);
FxResult fx_ui_dispatch_poll(FxNativeUiDispatch *,uintptr_t task,int visible,
                           FxResult (*flush)(FxPlatformOwner *));
uint32_t fx_ui_dispatch_wait(const FxNativeUiDispatch *,uintptr_t task,int visible,uint32_t original,uint32_t interval);
/* Native adapters: only the independently bound main-loop BL sites call these.
 * Nested/original timer helper calls remain original. No timer IDs are taken. */
void fx_native_ui_capture_task(void);
uint32_t fx_native_ui_poll_native(void *flags);
uint32_t fx_native_ui_wait_native(void);
#endif
