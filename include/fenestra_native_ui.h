#ifndef FENESTRA_NATIVE_UI_H
#define FENESTRA_NATIVE_UI_H
#include "fenestra_native_menu.h"
#include "fenestra_text_regions.h"
#define FX_NATIVE_UI_STRUCTURE UINT32_C(0x80000000) /* Internal repaint plan; not an SDK opcode. */
#define FX_NATIVE_UI_TITLE UINT32_C(0x40000000)
#define FX_NATIVE_UI_NOTICE UINT32_C(0x20000000)
#define FX_NATIVE_UI_PAGES (FX_UI_CONTROLS+2u)
#define FX_NATIVE_UI_ROW_LABELS (FX_UI_CONTROLS*2u+FX_UI_OPTIONS)
#define FX_NATIVE_UI_ROW_VALUES (FX_NATIVE_UI_ROW_LABELS+FX_UI_CONTROLS)
#define FX_NATIVE_UI_BACK_TEXT (FX_NATIVE_UI_ROW_VALUES+FX_UI_CONTROLS)
#define FX_NATIVE_UI_NOTICE_TEXT (FX_NATIVE_UI_BACK_TEXT+1u)
#define FX_NATIVE_UI_TEXTS (FX_NATIVE_UI_NOTICE_TEXT+1u)
#define FX_EACH_UI_SLOT(M) M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8) M(9) M(10) M(11) M(12) M(13) M(14) M(15)
#define FX_EACH_UI_OPTION(M) M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7)
typedef struct { FxUiControl control;uint32_t revision; } FxNativePickerView;
struct FxNativeAppLink;
/* Core-owned, immutable after publication. A view may own disjoint spans;
 * native IDs and logical page indices are deliberately independent. */
typedef struct FxUiPageSpan {
    struct FxNativeUi *ui;uint32_t first,count,logical,reserved;
    _Atomic uintptr_t domain;
} FxUiPageSpan;
typedef struct FxUiPageRoute {
    struct FxUiPageRoute *next;struct FxNativeUi *ui;FxUiPageSpan *span;
    _Atomic uintptr_t domain;
} FxUiPageRoute;
struct FxNativeUi;
/* Trusted core-owned projection, not module callbacks. Optional providers keep
 * the existing Nikon widget construction/input machinery for a copied window. */
typedef struct {
    void *(*allocate)(struct FxNativeUi *,uint32_t);
    void (*release)(struct FxNativeUi *,void *,uint32_t,uint32_t);
    FxResult (*snapshot)(struct FxNativeUi *,FxUiControl *,uint32_t *,uint32_t *,uint32_t *,uint8_t *);
    uint32_t (*destination)(struct FxNativeUi *,uint32_t);
    FxNativePage *(*page)(struct FxNativeUi *,uint32_t);
    struct FxNativeUi *(*resolve)(struct FxNativeUi *,uint32_t);
    FxResult (*retirement)(struct FxNativeUi *,uint32_t,uint32_t *);
    void (*closed)(struct FxNativeUi *,uint32_t);
} FxNativeUiSource;
typedef struct FxNativeUi {
    FxPlatformOwner *owner;FxNativeMenuProfile profile;
    void *(*allocate)(uint32_t);uint32_t page_base,text_base,row_base,editing,picker_completed;
    uintptr_t actions[FX_UI_CONTROLS],options[FX_UI_OPTIONS],callbacks[5];
    _Atomic(FxNativeMenu *) pages[FX_NATIVE_UI_PAGES];uint8_t pinned[FX_NATIVE_UI_PAGES];
    uint32_t layout_revisions[FX_NATIVE_UI_PAGES];
    FxUiControl controls[FX_UI_CONTROLS];uint32_t count,revision,structure_pending,title_pending;
    uint32_t control_revisions[FX_UI_CONTROLS],pending_control_revisions[FX_UI_CONTROLS],picker_rows_pending;
    uint32_t root_control; /* Optional PAGE id displayed directly at page_base. */
    uint64_t focus_tokens[FX_NATIVE_UI_PAGES];
    /* GUI-excluded staging, never a several-KiB native task stack copy. */
    FxUiControl pending[FX_UI_CONTROLS];
    uint8_t order[FX_UI_CONTROLS],pending_order[FX_UI_CONTROLS];
    _Atomic uint32_t presented_revision;
    uint16_t labels[FX_UI_CONTROLS][FX_UI_TEXT],values[FX_UI_CONTROLS][FX_UI_TEXT];
    uint16_t option_text[FX_UI_OPTIONS][FX_UI_TEXT];
    uint16_t contribution_title[FX_UI_TEXT]; /* Copied immutable loader identity. */
    /* Row-only presentation. Full labels/values remain available to titles,
     * dialogs and module inspection even when a narrow row needs ellipsis. */
    uint16_t row_labels[FX_UI_CONTROLS][FX_UI_TEXT],row_values[FX_UI_CONTROLS][FX_UI_TEXT];
    /* Two bounded, core-owned presentation banks. A native input pins its
     * observed bank while copying; the worker never overwrites a pinned one. */
    FxNativePickerView picker_views[2];
    _Atomic uint32_t picker_readers[2];
    _Atomic uint64_t picker_active;
    uint64_t picker_staged;
    uint32_t picker_staged_revision;
    const FxNativeDialogStyle *dialog;
    const struct FxNativeUiEditorProfile *editor_profile;
    _Atomic(struct FxNativeUiEditor *) editor;
    const struct FxNativeUiReaderProfile *reader_profile;
    _Atomic(struct FxNativeUiReader *) reader;
    uint32_t refresh_stage,refresh_row;FxResult refresh_result;
    void (*input_ready)(struct FxNativeUi *); /* Core-owned accepted-input notification. */
    _Atomic uintptr_t route_domain;uintptr_t route_next; /* Retained publication and diagnostic chain. */
    FxUiPageRoute page_routes[2];FxUiPageSpan page_span;
    FxTextRegions *text_regions;FxTextRegion text_region,reader_text_region;
    struct FxNativeAppLink *app_links; /* Immutable event tables retained with this view. */
    FxAllocationObserver observer;FxRetainedAccount retained_account;
    _Atomic uint32_t page_accounted[FX_NATIVE_UI_PAGES],editor_accounted,reader_accounted;
    const FxNativeUiSource *source;void *source_context;uint32_t picker_page;
    uint32_t projection_pending;uint64_t return_focus;
    /* Optional retained-view presentation port, installed before publication.
     * The owning original-UI loop consumes requests; input never allocates. */
    void (*request_refresh)(FxPlatformOwner *);
    uint32_t navigation_pending,navigation_source,navigation_target,navigation_slot,navigation_control,navigation_kind,navigation_parent;
    uint32_t recovery_notice,recovery_page,recovery_dirty,recovery_focus,recovery_restore;
    FxResult recovery_error;
    uint64_t recovery_return;
} FxNativeUi;
enum {FX_NATIVE_UI_RECOVER_NAVIGATION=1u,FX_NATIVE_UI_RECOVER_UPDATE=2u};
static inline uint32_t fx_native_ui_picker_id(const FxNativeUi *u) {
    return u->picker_page?u->picker_page:u->page_base+FX_NATIVE_UI_PAGES-1u;
}
static inline uint32_t fx_native_ui_slot(const FxNativeUi *u,uint32_t page) {
    if(!u)return UINT32_MAX;
    if(page==fx_native_ui_picker_id(u))return FX_NATIVE_UI_PAGES-1u;
    if(page==u->page_base)return 0u;
    return !u->source && page>=u->page_base && page-u->page_base<FX_NATIVE_UI_PAGES?page-u->page_base:UINT32_MAX;
}
static inline uint32_t fx_native_ui_page_id(const FxNativeUi *u,uint32_t slot) {
    return slot==FX_NATIVE_UI_PAGES-1u?fx_native_ui_picker_id(u):u->page_base+slot;
}
/* A previously closed generation cannot close or route into a newer page. */
int fx_native_ui_page_matches(const FxNativeUi *,const FxNativePage *);
/* Original GUI task and lock only. Prepare every replacement before committing
 * copied controls, native row order, text and event tables as one UI update. */
FxResult fx_native_ui_routes_prepare(FxNativeUi *,uint32_t count,FxNativeRowProgram *pending[FX_UI_CONTROLS]);
void fx_native_ui_routes_commit(FxNativeUi *,FxNativeRowProgram *pending[FX_UI_CONTROLS]);
void fx_native_ui_routes_abort(FxNativeUi *,FxNativeRowProgram *pending[FX_UI_CONTROLS]);
uint64_t fx_native_ui_window_focus(const FxNativeUi *);
void fx_native_ui_window_restore(FxNativeUi *,uint64_t focus);

static inline void *fx_native_ui_allocate(FxNativeUi *u,uint32_t bytes) {
    if(u->source && u->source->allocate)return u->source->allocate(u,bytes);
    void *p=u->allocate(bytes);if(p)fx_allocation_observe(u->observer,bytes,FX_ALLOC_REQUESTED);return p;
}
static inline void fx_native_ui_keep(FxNativeUi *u,_Atomic uint32_t *tag,uint32_t bytes) {
    if(u->observer.event){if(!atomic_exchange(tag,1u))fx_allocation_observe(u->observer,bytes,FX_ALLOC_PUBLISHED);}
    else fx_retained_account(u->retained_account,tag,bytes);
}
/* Cold activation seed for controller-owned views which predate modules.
 * Inline owner/view storage is already counted by its enclosing allocation. */
void fx_native_ui_account_retained(FxNativeUi *);
/* Original UI-task diagnostic; records the last native presentation step. */
static inline FxResult fx_native_ui_trace(FxNativeUi *u,uint32_t stage,uint32_t row,FxResult result) {
    if(u && (stage==1u || result!=FX_OK || u->refresh_result==FX_OK)){u->refresh_stage=stage;u->refresh_row=row;u->refresh_result=result;}return result;
}
FxResult fx_native_ui_init(FxNativeUi *,FxPlatformOwner *,const FxNativeMenuProfile *,void *(*allocate)(uint32_t),
                           uint32_t page_base,uint32_t text_base,uint32_t row_base,
                           const uintptr_t actions[FX_UI_CONTROLS],const uintptr_t options[FX_UI_OPTIONS],
                           const uintptr_t callbacks[5]);
FxNativePage *fx_native_ui_page(FxNativeUi *,uint32_t page);
/* Shared renderer after a core projection has selected its concrete view. */
FxNativePage *fx_native_ui_view_page(FxNativeUi *,uint32_t page);
FxResult fx_native_ui_action(FxNativeUi *,uint32_t slot);
FxResult fx_native_ui_option(FxNativeUi *,uint32_t option);
/* Original UI task + GUI exclusion. Route by the source page's owned
 * descriptor/event pair, not current-page state or a compiled owner index. */
FxResult fx_native_ui_event(FxNativeUi *,const FxNativePage *,const FxNativeEvent *);
int fx_native_ui_row_visible(const FxNativeUi *,uint32_t page,uint32_t row);
int fx_native_ui_row_submenu(const FxNativeUi *,uint32_t row);
FxResult fx_native_ui_open(FxNativeUi *,uint32_t page);
FxResult fx_native_ui_close(FxNativeUi *,uint32_t page);
uint32_t fx_native_ui_value(FxNativeUi *,uint32_t row,int *enabled);
int fx_native_ui_text(FxNativeUi *,uint32_t id,const uint16_t **text,uint32_t *bytes);
/* Caller owns Nikon's GUI exclusion. Does not replace live page/widget
 * storage. Structural revisions request native visibility/reflow, including
 * the eight stable native option slots of an open picker. */
FxResult fx_native_ui_refresh_values(FxNativeUi *,uint32_t *changed_rows);
/* Original UI task + GUI exclusion. A prepared destination is navigated only
 * AFTER the caller releases that exclusion. Failure keeps the source open. */
FxResult fx_native_ui_prepare_navigation(FxNativeUi *,const FxNativePage *,uint32_t *destination);
/* Draw a retained Retry row for recoverable list-update failures. An error
 * notice never acknowledges a newer logical revision that was not applied. */
FxResult fx_native_ui_recover_values(FxNativeUi *,uint32_t page,uint32_t *changed_rows);
/* Convert logical control/option bits to current descriptor row positions.
 * A layout change invalidates every row, including the core-owned Back row. */
uint32_t fx_native_ui_repaint_rows(const FxNativeUi *,const FxNativePage *,uint32_t changes);
/* Called only after the native display transaction submits successfully,
 * still under its GUI exclusion. Publishes the input snapshot and UI ack. */
FxResult fx_native_ui_presented(FxNativeUi *,uint32_t revision);
/* Core-side bounded read of the last native presentation, never a staged
 * but unsubmitted list. No module-memory pointers or allocations. */
FxResult fx_native_ui_picker_snapshot(FxNativeUi *,FxNativePickerView *);
/* Original UI task + GUI exclusion. Reorder paired descriptors/event pointers;
 * retired native object storage remains allocated, but loses descriptor identity.
 * Caller first hides retired widgets, then runs original native reflow. */
FxResult fx_native_ui_order_page(FxNativeUi *,FxNativePage *);
/* Decide under the owner's copied-data lock; actual navigation must occur on
 * the original UI task OUTSIDE the nonrecursive GUI exclusion. */
FxResult fx_native_ui_retirement_target(FxNativeUi *,uint32_t page,uint32_t *destination);
/* Hide a module contribution after it explicitly removes/hides its entire
 * declared UI. A module with no copied controls retains its legacy fallback. */
int fx_native_ui_root_visible(FxNativeUi *);
void fx_native_ui_identity(FxNativeUi *,const char *);
#endif
