#ifndef FENESTRA_NATIVE_UI_PAGER_H
#define FENESTRA_NATIVE_UI_PAGER_H
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_font.h"
#include "fenestra_capacity.h"
#define FX_NATIVE_UI_PAGE_ITEMS 14u
#define FX_NATIVE_UI_PAGER_BUDGET (64u*1024u*1024u)
/* One target-independent ID space. The profile supplies its independently
 * checked free namespace, normal heap, and native text-table growth adapter. */
typedef struct FxNativeUiSpace {
    FxNativeUiDispatch *domain;FxIndexMemory memory;
    void *text_context;FxResult (*ensure_text)(void *,uint32_t);
    uint32_t first,limit,text_base,text_stride,extra_text_base;
    _Atomic uint32_t next;
    _Atomic uint32_t next_text;
    _Atomic uintptr_t text_routes[64]; /* Immutable ranges, 1024 IDs per bucket. */
    FxCapacityDomain *page_capacity,*text_capacity; /* Configured before service publication. */
} FxNativeUiSpace;
typedef struct {uint32_t abi,bytes,page_first,page_limit,page_next,text_first,text_limit,text_next;} FxNativeUiSpaceInfo;
FxResult fx_native_ui_space_info(const FxNativeUiSpace *,FxNativeUiSpaceInfo *);
FxResult fx_native_ui_space_observe(FxNativeUiSpace *,FxCapacityDomain *pages,FxCapacityDomain *texts);
FxResult fx_native_ui_space_init(FxNativeUiSpace *,FxNativeUiDispatch *,FxIndexMemory,
    void *,FxResult (*ensure_text)(void *,uint32_t),uint32_t first,uint32_t limit,
    uint32_t text_base,uint32_t text_stride,uint32_t extra_text_base);
FxResult fx_native_ui_space_take(FxNativeUiSpace *,uint32_t count,uint32_t *first);
/* Unpublished tail reservation only. Never recycle registered IDs. */
void fx_native_ui_space_rollback(FxNativeUiSpace *,uint32_t first,uint32_t count);
/* Original UI task, registered root view, no open pages. Every allocation is
 * core-owned. Large logical graphs use copied page windows and native widgets.
 * Live list membership is copied transactionally; modal identities stay pinned. */
FxResult fx_native_ui_pager_enable(FxNativeUi *,FxNativeUiSpace *);
/* Trusted core diagnostics on the original GUI task only. The mutable cold
 * cache never escapes this task; published page/vector identities stay stable. */
typedef struct {
    uint32_t abi,bytes,nodes,index_capacity,cache_buckets,cache_rehashes,used,peak;
    uint64_t lookups,probes;
} FxNativeUiPagerInfo;
FxResult fx_native_ui_pager_info(FxNativeUi *,FxNativeUiPagerInfo *);
int fx_native_ui_pager_text(FxNativeUiSpace *,uint32_t id,const uint16_t **,uint32_t *);
#endif
