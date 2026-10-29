#ifndef FENESTRA_SERVICES_H
#define FENESTRA_SERVICES_H
#include "fenestra_api.h"
#include "fenestra.h"
#include "fenestra_module_index.h"
#include "fenestra_module.h"

/* Integer, pointer-free wire messages. A worker copies requests only after
 * EL0 has returned. Services run on original Nikon tables, never in a trap. */
#define FX_SERVICE_ABI 1u
#define FX_SERVICE_BYTES 768u
#define FX_SERVICE_REGIONS 8u
#define FX_SERVICE_PATCHES 8u
#define FX_SERVICE_PATCH_BYTES 64u
#define FX_SERVICE_EVENTS 8u
#define FX_MODULE_OWNER_LIMIT 65534u
/* Installed identities are budgeted by FxModuleIndex, not owner count. */
#define FX_SDK_QUERY (1u<<16)
#define FX_SDK_MEMORY (1u<<17)
#define FX_SDK_FILES (1u<<18)
#define FX_SDK_NATIVE (1u<<19)
#define FX_SDK_MENU (1u<<20)
#define FX_SDK_BINDING (1u<<21)
#define FX_SDK_SIMD (1u<<22) /* Full Q0..Q31/FPCR/FPSR module continuation. */
#define FX_SDK_CAPS (FX_SDK_QUERY|FX_SDK_MEMORY|FX_SDK_FILES|FX_SDK_NATIVE|FX_SDK_MENU|FX_SDK_BINDING|FX_SDK_SIMD)
enum { FX_S_QUERY=1,FX_S_LOG,FX_S_EVENT,FX_S_REGION_INFO,FX_S_READ,FX_S_WRITE,
       FX_S_PATCH_PREPARE,FX_S_PATCH_COMMIT,FX_S_PATCH_RESTORE,FX_S_PATCH_ABORT,
       FX_S_FILE_READ,FX_S_FILE_CREATE,FX_S_NATIVE,FX_S_MENU,FX_S_BINDING,
       FX_S_PANIC };
enum { FX_EVENT_INIT=1,FX_EVENT_TICK,FX_EVENT_ACTION,FX_EVENT_VIEW,FX_EVENT_STOP };
enum { FX_REGION_READ=1,FX_REGION_WRITE=2,FX_REGION_PATCH=4 };
typedef struct {
    uint32_t sequence,operation,generation,bytes;
    uint64_t arg[4];
    uint8_t data[FX_SERVICE_BYTES];
} FxServiceRequest;
typedef struct {
    uint32_t sequence,result,bytes,reserved;
    uint64_t value[4];
    uint8_t data[FX_SERVICE_BYTES];
} FxServiceReply;
typedef struct { FxServiceRequest request;FxServiceReply reply; } FxServiceExchange;
/* Storage is a native page-rounded copy of this wire record. Modules may use
 * only FxServiceExchange; unused historical arena space is not an SDK buffer. */
#define FX_SERVICE_EXCHANGE_BYTES ((uint32_t)((sizeof(FxServiceExchange)+4095u)/4096u*4096u))
typedef struct {
    FxModuleServices base;
    uint32_t service_abi,owner,generation,capabilities;
    uint64_t time_ms;
    FxServiceExchange *exchange;
    uint32_t event,event_value;
} FxServiceModule;
typedef struct {
    char id[16],file[32];uint8_t digest[32];
    uint32_t bytes,required,optional,enabled,period_ms,budget_ms,dependencies,image_bytes;
    FxModuleStacks stacks;
} FxDiscoveredModule;
/* Discovery owns this movable, transient RAM vector. No native callback may
 * retain an entry pointer. Runtime owners copy descriptors into stable state.
 * Pass a zero-initialized output; a live output returns BUSY, unchanged.
 * Destroy a successful snapshot once its descriptors have been copied. */
typedef struct {
    uint32_t count;FxDiscoveredModule *entries;FxModuleIndex index;
} FxModuleInventory;
void fx_modules_destroy(FxModuleInventory *);

/* Transient discovery results, never a file/allowlist. Module identity is its
 * filename; target/capabilities/body digest are read from the .wlm itself. */
typedef struct { char name[32];uint64_t bytes;uint32_t attributes; } FxModuleDirectoryEntry;
typedef struct {
    void *context;
    FxResult (*open)(void *);
    FxResult (*next)(void *,FxModuleDirectoryEntry *);
    FxResult (*close)(void *);
    FxResult (*read)(void *,const char *,uint8_t *,uint32_t,uint32_t *);
} FxModuleDirectory;
/* Normal-task synchronous read: returning releases all buffer references,
 * including on close error. Temporary package RAM is freed on every path.
 * Only enabled packages need this; the returned module remains unprepared. */
FxResult fx_module_directory_inspect(const FxModuleDirectory *,const char *,uint64_t bytes,
    const uint8_t firmware[32],uint64_t capabilities,FxIndexMemory,uint8_t *scratch,uint32_t capacity,
    FxModule *,uint8_t digest[32]);
FxResult fx_modules_discover(const FxModuleDirectory *,const uint8_t firmware[32],
    uint32_t capabilities,uint8_t *scratch,uint32_t capacity,FxModuleInventory *,FxIndexMemory);
/* Trusted cold allocation observation; original wrappers above remain valid.
 * Index and extra package allocations are disjoint from borrowed scratch. */
FxResult fx_module_directory_inspect_accounted(const FxModuleDirectory *,const char *,uint64_t bytes,
    const uint8_t firmware[32],uint64_t capabilities,FxIndexMemory,uint8_t *scratch,uint32_t capacity,
    FxModule *,uint8_t digest[32],FxAllocationObserver);
FxResult fx_modules_discover_accounted(const FxModuleDirectory *,const uint8_t firmware[32],
    uint32_t capabilities,uint8_t *scratch,uint32_t capacity,FxModuleInventory *,FxIndexMemory,
    FxAllocationObserver indexes,FxAllocationObserver packages);
typedef struct {
    uint8_t *data;uint32_t bytes,flags,id;char name[32];
} FxServiceRegion;
typedef struct {
    uint32_t region,offset,bytes;
    uint8_t before[FX_SERVICE_PATCH_BYTES],after[FX_SERVICE_PATCH_BYTES];
} FxServicePatch;
typedef struct {
    void *context;
    FxResult (*call)(void *,const FxServiceRequest *,FxServiceReply *);
    /* Close admission before returning. panic=1 revokes/retains resources;
     * normal stop returns BUSY until independently acknowledged drain.
     * Panic BUSY defers notification; the retained worker retries it. */
    FxResult (*stop)(void *,uint32_t panic);
} FxServiceBackend;
enum { FX_SERVICE_STOP_NONE,FX_SERVICE_STOP_PENDING,FX_SERVICE_STOP_DONE,FX_SERVICE_STOP_FAULT,
       FX_SERVICE_STOP_FAULT_PENDING };
typedef struct {
    uint32_t owner,generation,capabilities,active,last_sequence,region_count;
    uint32_t patch_count,patch_committed,revision,fault,events_head,events_count,stop_state;
    uint64_t time_ms;
    FxServiceRegion regions[FX_SERVICE_REGIONS];
    FxServicePatch patches[FX_SERVICE_PATCHES];
    uint32_t events[FX_SERVICE_EVENTS][2];
    FxServiceRequest last_request;
    FxServiceReply last_reply;
    FxServiceBackend backend;
    char last_log[128];
    /* Core-only, immutable before task publication; not part of the module ABI. */
    void *wake_context;void (*wake_callback)(void *);
} FxServiceOwner;
int fx_service_name(const char *,uint32_t,int);
void fx_service_init(FxServiceOwner *,uint32_t,uint32_t,uint32_t,FxServiceBackend);
FxResult fx_service_region(FxServiceOwner *,const char *,void *,uint32_t,uint32_t,uint32_t *);
FxResult fx_service_event(FxServiceOwner *,uint32_t,uint32_t);
FxResult fx_service_dispatch(FxServiceOwner *,const FxServiceRequest *,FxServiceReply *);
FxResult fx_service_stop(FxServiceOwner *);
void fx_service_fault(FxServiceOwner *);
/* One nonblocking retry on the retained owner worker; never resume EL0. */
void fx_service_fault_poll(FxServiceOwner *);
#endif
