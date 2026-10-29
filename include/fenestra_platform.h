#ifndef FENESTRA_PLATFORM_H
#define FENESTRA_PLATFORM_H
#include "fenestra_services.h"
#include "fenestra_native_call.h"
#include "fenestra_module.h"
#include "fenestra_binding_store.h"
#include "fenestra_accounting.h"
#include <stdatomic.h>

/* Target-neutral, copied-message SDK. Names are contracts; addresses belong
 * to a verified firmware profile or an explicit module-owned profile. */
#define FX_PLATFORM_ABI 1u
/* Inline storage and current native view width, not logical identity limit. */
#define FX_UI_CONTROLS 16u
#define FX_UI_CONTROL_BUDGET (1024u * 1024u)
#define FX_UI_EVENTS 16u
#define FX_UI_EVENT_BUDGET (1024u * 1024u)
#define FX_UI_TEXT 48u
#define FX_UI_INPUT_TEXT 128u /* Copied UTF-8 input including NUL; not row text size. */
#define FX_UI_OPTIONS 8u
/* UI PUT reply.value[1] is the input serial fence at publication;
 * GET reply.value[2] is the sampled input serial. Existing revision
 * fields and wire structure are unchanged. Useful for virtual rows. */
enum { FX_S_PLATFORM=22u,FX_S_UI=23u,FX_S_LIFECYCLE=24u };
enum { FX_BIND_FUNCTION=1u,FX_BIND_TABLE,FX_BIND_HOOK,FX_BIND_DEVICE };
enum { FX_CALL_U64_8=1u,FX_CALL_BYTES,FX_CALL_EVENTS,FX_CALL_DEVICE };
enum { FX_CALL_AAPCS64=5u };
enum { FX_BIND_READ=1u,FX_BIND_WRITE=2u,FX_BIND_ESCAPE=4u,FX_BIND_PROVIDER=8u };
enum { FX_BIND_DESCRIBE,FX_BIND_OPEN,FX_BIND_CLOSE,FX_BIND_CALL,FX_BIND_COPY,FX_BIND_STORE };
enum { FX_BIND_INFO=6u,FX_BIND_ENUMERATE=7u,FX_BIND_DECLARE=8u,FX_BIND_CAPACITY=9u };
/* Allocated metadata capacities, not a promise of free native RAM. */
typedef struct {uint32_t abi,bytes,lease_slots,active_leases,declaration_slots,declarations,
    budget,used,peak,identities_remaining,inline_leases,inline_declarations,profile_bindings,allocator_available,reserved[2];} FxBindingCapacity;
typedef struct {
    uint32_t abi,bytes,binding_count,capabilities,ui_controls,ui_events,owner,generation;
    uint8_t identity[32];char model[48],firmware[32];
} FxPlatformInfo;
typedef struct {
    char name[64];uint32_t kind,abi,version,flags,capabilities,bytes,available,reserved;
    uint8_t original[16];uint64_t address;
} FxBindingInfo;
enum { FX_RESOURCE_ATTACH=1u,FX_RESOURCE_DETACH,FX_RESOURCE_CALL,FX_RESOURCE_PANIC };
enum { FX_MODULE_APP=1u,FX_MODULE_EXTENSION,FX_MODULE_SERVICE };
enum { FX_OWNER_RUNNING=1u,FX_OWNER_FINISHED,FX_OWNER_STOPPING,FX_OWNER_QUIESCED,FX_OWNER_FAULT };
enum { FX_LIFE_QUERY,FX_LIFE_CONFIGURE,FX_LIFE_FINISH };
enum { FX_UI_PAGE=1u,FX_UI_ACTION,FX_UI_SWITCH,FX_UI_CHOICE,FX_UI_DIALOG,FX_UI_EDIT,FX_UI_DOCUMENT };
enum { FX_UI_PUT,FX_UI_HIDE,FX_UI_GET,FX_UI_INPUT,FX_UI_POLL,FX_UI_QUERY,FX_UI_FOCUS };
enum { FX_UI_REMOVE=7u,FX_UI_REORDER=8u,FX_UI_STATUS=9u,FX_UI_QUEUE=10u,FX_UI_CONTROL_CAPACITY=11u,FX_UI_MOVE=12u };
enum { FX_INPUT_ACTIVATE=1u,FX_INPUT_CHANGE,FX_INPUT_ACCEPT,FX_INPUT_CANCEL,FX_INPUT_FOCUS,FX_INPUT_BLUR,FX_INPUT_TEXT };
enum { FX_UI_ENABLED=1u,FX_UI_VISIBLE=2u,FX_UI_ROOT=4u };
enum { FX_UI_RETIRED=8u }; /* Core snapshot tombstone; never accepted by PUT. */
enum { FX_UI_CAP_ROOT=1u,FX_UI_CAP_FOCUS=2u,FX_UI_CAP_LIVE_ROWS=4u,FX_UI_CAP_LIVE_TITLES=8u,
       FX_UI_CAP_EVENT_REVISION=16u,FX_UI_CAP_LIVE_PICKER=32u,FX_UI_CAP_REMOVE=64u,
       FX_UI_CAP_REORDER=128u,FX_UI_CAP_SUBMISSION=256u,FX_UI_CAP_MODAL=512u,FX_UI_CAP_TEXT_EVENTS=1024u,FX_UI_CAP_EDIT=2048u,FX_UI_CAP_DOCUMENT=4096u,FX_UI_CAP_QUEUE=8192u,FX_UI_CAP_CONTROL_CAPACITY=16384u,FX_UI_CAP_MOVE=32768u };
typedef struct {uint32_t abi,bytes,capabilities,controls,events,options,text_bytes,reserved;} FxUiInfo;
/* A native compositor submission ACK, not a physical LCD scanout fence.
 * Matching revisions AND focus epochs identify the actually submitted view. */
typedef struct {
    uint32_t abi,bytes,revision,submitted_revision,control,foreground,focus_epoch,submitted_epoch,phase,reserved;
} FxUiStatus;
typedef struct {
    uint32_t abi,bytes,control,foreground;
    uint64_t epoch;
    uint32_t dropped_inputs,phase;
} FxUiFocus;


typedef struct {
    uint32_t abi,bytes,id,parent,kind,flags,value,option_count;
    char label[FX_UI_TEXT],text[FX_UI_TEXT],options[FX_UI_OPTIONS][32];
} FxUiControl;
/* Native action/change/accept/cancel: reserved is the observed control
 * revision. Focus/blur retain the existing low32 focus epoch contract. */
typedef struct { uint64_t serial;uint32_t control,event,value,reserved; } FxUiEvent;
/* Only FX_INPUT_TEXT extends the unchanged 24-byte prefix. value is UTF-8
 * bytes excluding NUL; reserved is the observed control revision. Native
 * input copies the whole result before returning; later PUT cannot alter it. */
typedef struct {FxUiEvent event;char text[FX_UI_INPUT_TEXT];} FxUiTextEvent;
typedef struct {uint32_t abi,bytes,capacity,count;uint64_t used,peak,limit;
    uint32_t dropped,pending_cancel;} FxUiQueueInfo;
/* Extra graph storage only; inline controls are already owned by the parent.
 * native_window describes the current renderer, not the logical graph limit.
 * BOUNDED is cleared when the renderer gains automatic native pagination. */
enum {FX_UI_CONTROL_NATIVE_BOUNDED=1u,FX_UI_CONTROL_ALLOCATOR=2u};
typedef struct {uint32_t abi,bytes,slots,identities,live,retired,native_window,flags;
    uint64_t used,peak,limit,reserved;} FxUiControlCapacity;
typedef struct FxUiControlStore FxUiControlStore;
/* panic_status is zero until a terminal provider reply, then FxResult+1.
 * BUSY stays pending. Neither acknowledgement nor failure releases storage. */
typedef struct FxPlatformOwner FxPlatformOwner;
/* Core-owned, append-only provider lifetimes. A published node/context may
 * still be referenced by native callbacks after its logical handle closes. */
typedef struct FxProviderContext FxProviderContext;
struct FxProviderContext {FxProviderContext *next;const char *name;void *context;};

typedef struct {
    void *context;
    FxResult (*read)(void *,uint64_t,void *,uint32_t);
    FxResult (*write)(void *,uint64_t,const void *,uint32_t);
    FxResult (*call)(void *,const FxNamedBinding *,const uint64_t[8],uint64_t *);
    /* Attach retains the native provider until an acknowledged detach.
     * Panic requests stop but NEVER treats it as a detach acknowledgement. */
    FxResult (*resource)(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t token,uint64_t *cookie,
                         const FxServiceRequest *,FxServiceReply *);
    FxResult (*scalar)(void *,uint64_t address,uint32_t bytes,uint64_t *value,uint32_t write);
    FxResult (*cache)(void *,uint64_t address,uint32_t bytes,uint32_t operation);
    FxResult (*call_frame)(void *,const FxNamedBinding *,FxNativeCallFrame *);
} FxPlatformIO;
typedef struct {
    uint32_t abi,count;
    const char *model,*firmware;
    uint8_t identity[32];
    const FxNamedBinding *bindings;
    FxPlatformIO io;
    uint8_t module_identity[32]; /* Package target identity; can differ from application SHA. */
} FxPlatform;

struct FxPlatformOwner {
    const FxPlatform *platform;
    uint32_t owner,generation,capabilities,kind;
    _Atomic uint32_t first_error;
    _Atomic uint32_t phase;
    uint32_t binding_count,control_count;
    _Atomic uint32_t revision;
    uint32_t configured;
    uint64_t next_token,event_serial;
    FxBindingStore bindings;
    FxUiControl controls[FX_UI_CONTROLS];
    uint32_t control_revisions[FX_UI_CONTROLS];
    FxUiEvent events[FX_UI_EVENTS];
    uint32_t event_head,event_count;
    _Atomic uint32_t ui_lock,binding_lock,dropped_inputs,ui_references;
    void *native_resources; /* Core-owned provider state; never module memory. */
    _Atomic(FxProviderContext *) provider_contexts;
    FxAccountState *accounting; /* Trusted runtime-owned state; not module memory. */
    FxAllocationObserver (*account_observer)(FxPlatformOwner *,const char *);
    struct FxCapacityRegistry *capacity; /* Trusted activated shared native domains. */
    struct FxCapacityWatch *capacity_watches; /* Owner-locked, retained subscriptions. */
    _Atomic(void *) ui_documents; /* Immutable copied text versions, allocated on demand. */
    /* Trusted Loader snapshot. Installed once, after mapping publication and
     * before the first generic service call. Never populated from a request. */
    uint8_t *module_image;
    uint32_t module_bytes,module_segment_count;
    FxSegment module_segments[FX_MODULE_SEGMENTS];
    /* Native GUI publishes control/foreground/epoch in one atomic word.
     * Independent of the finite event queue and the worker's UI lock. */
    _Atomic uint64_t ui_focus;
    /* Copied native pages/callbacks remain allocated until reboot. These
     * passive references do not mean a display provider or DMA is active. */
    _Atomic uint32_t ui_page_references;
    /* Stable control slots/IDs are never recycled while native callbacks can
     * retain them. Display order is separate; retirement is distinct from HIDE. */
    uint8_t ui_order[FX_UI_CONTROLS];
    _Atomic uint64_t ui_submission; /* revision<<32 | original focus epoch */
    char event_text[FX_UI_EVENTS][FX_UI_INPUT_TEXT]; /* Fixed queue storage, no GUI allocation. */
    /* One native picker per owner. Closing cannot wait for its worker's UI
     * lock/queue; retain its terminal event until POLL creates queue space.
     * Native GUI is the sole producer; ui_lock serializes consumers.
     * 0 empty, 1 producer copying, 2 published. Never recycle owner storage. */
    _Atomic uint32_t ui_cancel_state;
    FxUiEvent ui_cancel;
    /* Core UI dispatcher nodes. Register before publishing/starting this
     * owner; once registered, this storage and its identity stay until reboot.
     * No per-publication allocation or global owner-slot bit is required. */
    _Atomic uintptr_t ui_dispatch_domain;
    uintptr_t ui_dispatch_registered_next,ui_dispatch_ready_next;
    _Atomic uint32_t ui_dispatch_queued;
    _Atomic uintptr_t native_ui; /* Profile-created GUI object, retained until reboot. */
    _Atomic uint32_t ui_presence; /* count low 31, visible bit 31; lock-free menu probe. */
    uint32_t ui_presence_revision; /* Only accessed while ui_lock is held. */
    void *wake_context; /* Immutable Loader-owned callback context. */
    _Atomic uintptr_t wake_callback;
    _Atomic uint32_t tick_subscriptions;
    /* Only ui_lock readers access storage. Normal worker requests resize;
     * native input/focus/hook/cancel callbacks never allocate. */
    FxIndexMemory ui_event_memory;FxUiTextEvent *ui_event_buffer;
    _Atomic uint32_t ui_event_capacity;
    uint32_t ui_event_bytes,ui_event_peak;FxAllocationObserver ui_event_observer;
    FxUiControlStore *ui_controls;FxIndexMemory ui_control_memory;uint32_t ui_control_budget;
};
static inline FxAllocationObserver fx_platform_account_observer(FxPlatformOwner *owner,const char *name) {
    return owner && owner->account_observer?owner->account_observer(owner,name):(FxAllocationObserver){0};
}
/* Notification producers publish their queue/data before this coalescing
 * wake. A wake never executes module code on the producer's task/IRQ. */
static inline void fx_platform_wake(FxPlatformOwner *owner) {
    if(!owner)return;
    uintptr_t callback=atomic_load_explicit(&owner->wake_callback,memory_order_acquire);
    if(callback)((void (*)(void *))callback)(owner->wake_context);
}
/* Core publication stamp, not an SDK operation or a rendered-frame fence.
 * Reading an owner concurrently with original GUI input must be atomic. */
static inline uint64_t fx_platform_ui_stamp(const FxPlatformOwner *owner) {
    return ((uint64_t)atomic_load_explicit(&owner->revision,memory_order_acquire)<<32)|
        atomic_load_explicit(&owner->phase,memory_order_acquire);
}
static inline int fx_platform_ui_changed(const FxPlatformOwner *owner,uint64_t before) {
    uint64_t after=fx_platform_ui_stamp(owner);uint32_t phase=(uint32_t)after;
    return after!=before && (phase==FX_OWNER_RUNNING || phase==FX_OWNER_FINISHED);
}

FxResult fx_platform_init(FxPlatformOwner *,const FxPlatform *,uint32_t owner,
                          uint32_t generation,uint32_t capabilities);
FxResult fx_platform_dispatch(FxPlatformOwner *,const FxServiceRequest *,FxServiceReply *);
const FxNamedBinding *fx_platform_binding(const FxPlatformOwner *,uint32_t index);
const FxNamedBinding *fx_platform_find(const FxPlatformOwner *,const char *name);
/* Internal native providers: binding_lock is already held. */
const FxNamedBinding *fx_platform_open_binding(const FxPlatformOwner *,uint64_t token);
FxResult fx_platform_stop(FxPlatformOwner *,uint32_t panic);
FxResult fx_platform_bind_module(FxPlatformOwner *,const FxModule *,void *image,uint32_t capacity);
/* Worker-only, after EL0 has completely returned on original tables. Validate
 * every segment before copying; no native callback retains a module pointer. */
FxResult fx_platform_module_transfer(FxPlatformOwner *,uint64_t address,void *,uint32_t bytes,uint32_t write_to_module);
/* Native GUI uses copied snapshots. Active provider references must drain
 * before QUIESCED. Passive page references retain core-owned storage through
 * reboot; they may close after QUIESCED and never acknowledge hardware stop. */
FxResult fx_ui_snapshot(FxPlatformOwner *,FxUiControl *,uint32_t capacity,uint32_t *count,uint32_t *revision);
FxResult fx_ui_presence(FxPlatformOwner *,uint32_t *count,int *visible);
/* Copied range with wide stable order indices. first is a creation-slot
 * cursor; total/revision describe the same locked snapshot. Callers retry
 * from zero if revisions differ between chunks. Native legacy snapshots
 * below deliberately retain their explicit caller-capacity check. */
FxResult fx_ui_snapshot_range(FxPlatformOwner *,uint32_t first,FxUiControl *,uint32_t capacity,
    uint32_t *count,uint32_t *total,uint32_t *revision,uint32_t *versions,uint32_t *order);
/* Native viewport only. parent=0 selects the live declared ROOT or implicit
 * wrapper. Copy visible, unretired direct children from the logical display
 * order, but keep output slots in creation order and return their local
 * display permutation. capacity<=FX_UI_CONTROLS is a physical window width,
 * not a logical owner limit. One lock/revision covers parent and children. */
FxResult fx_ui_project_page(FxPlatformOwner *,uint32_t parent,uint32_t first,FxUiControl *parent_copy,
    FxUiControl *children,uint32_t capacity,uint32_t *count,uint32_t *total,uint32_t *revision,
    uint32_t *versions,uint8_t *order);
/* Refresh a trusted copied modal window without changing its slot membership.
 * Missing/retired identities cannot silently acquire another control's data. */
FxResult fx_ui_snapshot_selected(FxPlatformOwner *,const FxUiControl *selected,uint32_t count,
    FxUiControl *out,uint32_t *versions,uint32_t *revision);
FxResult fx_ui_snapshot_versioned(FxPlatformOwner *,FxUiControl *,uint32_t capacity,uint32_t *count,
                                 uint32_t *revision,uint32_t control_revisions[FX_UI_CONTROLS]);
FxResult fx_ui_snapshot_layout(FxPlatformOwner *,FxUiControl *,uint32_t capacity,uint32_t *count,
                              uint32_t *revision,uint32_t control_revisions[FX_UI_CONTROLS],
                              uint8_t order[FX_UI_CONTROLS]);
FxResult fx_ui_route(FxPlatformOwner *,uint32_t control,uint32_t event,uint32_t value);
/* Core-owned native snapshot only; never exposed as an EL0 input request.
 * A stale choice event retains its old definition/revision without assigning
 * that old index to a new logical list. */
FxResult fx_ui_route_observed(FxPlatformOwner *,const FxUiControl *,uint32_t revision,uint32_t event,uint32_t value);
/* Native navigation has no module event, but must not enter a retired or
 * disabled copied control while a newer GUI update is still pending. */
FxResult fx_ui_observed_active(FxPlatformOwner *,const FxUiControl *);
/* Native close only, not a module input opcode. Reopening a picker must wait
 * while ui_cancel_state is nonzero; at most one terminal event is outstanding.
 * OK acknowledges retained delivery, not necessarily immediate queue insertion. */
FxResult fx_ui_defer_cancel(FxPlatformOwner *,const FxUiControl *,uint32_t revision,uint32_t value);
FxResult fx_ui_reference(FxPlatformOwner *,int retain);
FxResult fx_ui_page_reference(FxPlatformOwner *,int retain);
FxResult fx_ui_page_index(FxPlatformOwner *,uint32_t control,uint32_t *index);
FxResult fx_ui_page_slot(FxPlatformOwner *,uint32_t control,uint32_t *slot);
/* Native UI only: EMPTY for a live control; a retired control returns the
 * nearest surviving PAGE slot, or UINT32_MAX for the owner's native parent. */
FxResult fx_ui_retired_parent_slot(FxPlatformOwner *,uint32_t control,uint32_t *slot);
/* Native GUI only. An old page's close cannot revoke a newer page's token. */
FxResult fx_ui_focus_open(FxPlatformOwner *,uint32_t control,uint64_t *token);
FxResult fx_ui_focus_close(FxPlatformOwner *,uint64_t token);
FxResult fx_ui_focus_read(const FxPlatformOwner *,FxUiFocus *);
/* Worker-side nonwaiting dequeue, also used by a rendered-frame reply. */
FxResult fx_ui_poll_event(FxPlatformOwner *,FxUiEvent *);
/* Both UI POLL and scene submission use this copied-message dequeue. An
 * undersized output returns BOUNDS without consuming the pending event. */
FxResult fx_ui_poll_message(FxPlatformOwner *,void *,uint32_t capacity,uint32_t *bytes);
/* Native input only. bytes includes NUL; no embedded NUL or invalid UTF-8.
 * Delivers observed revision + immutable text; never overwrites current PUT
 * state. Applies to the native form's declared control, not a new UI kind. */
FxResult fx_ui_route_text_observed(FxPlatformOwner *,const FxUiControl *,uint32_t revision,
                                  const char *,uint32_t bytes);
FxResult fx_platform_retain_context(FxPlatformOwner *,FxProviderContext *);
/* Core provider preparation while caller holds owner.binding_lock; snapshot
 * is immutable and can execute on the selected original task after unlock. */
FxResult fx_platform_prepare_native(FxPlatformOwner *,uint64_t,const void *,uint32_t,FxNamedBinding *);
FxResult fx_platform_invoke_native(FxPlatformOwner *,const FxNamedBinding *,void *,uint32_t,uint64_t[2]);
FxResult fx_platform_notify(FxPlatformOwner *,uint64_t binding,uint32_t event,uint32_t value);
/* binding_lock held; queues only, caller unlocks then performs one wake. */
FxResult fx_platform_notify_locked(FxPlatformOwner *,uint64_t,uint32_t,uint32_t);
#endif
