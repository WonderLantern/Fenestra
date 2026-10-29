#ifndef FENESTRA_MODULE_MANAGER_H
#define FENESTRA_MODULE_MANAGER_H
#include "fenestra_platform.h"
#define FX_MANAGED_ROWS 10u
#define FX_MANAGER_PREVIOUS 14u
#define FX_MANAGER_NEXT 15u
#define FX_MANAGER_SEARCH 16u
#define FX_MANAGER_FILTER_BYTES 37u /* Native ASCII editor: 36 characters + NUL. */
typedef struct {
    char id[16],file[32];
    uint32_t present,enabled,current,valid,error,bytes,required,image_bytes;
    uint8_t digest[32];
} FxManagedModule;
typedef struct {
    FxModuleDirectory directory;
    FxResult (*rename)(void *,const char *,const char *);
    void *context;
    uint32_t (*current)(void *,const char *id);
    FxIndexMemory memory;uint32_t index_budget;
} FxModuleManagerIO;
typedef struct {
    FxModuleManagerIO io;FxPlatformOwner ui;
    uint8_t firmware[32];uint32_t capabilities;
    FxModuleIndex index;FxManagedModule *entries;
    uint32_t count,scanned,error,activated,uncertain,loading;
    _Atomic uint32_t lock,scan_requested,shutdown;
    uint8_t scratch[16385];
    uint32_t page,view_start,view_count;
    struct {char id[16];uint32_t revision;uint64_t serial_floor;} rows[FX_MANAGED_ROWS];
    uint32_t last_ui_revision;uint64_t last_ui_serial;
    FxServiceRequest ui_request;FxServiceReply ui_reply;
    uint32_t ui_pending; /* manager.lock; retry copied UI publication, not card IO */
    _Atomic uint32_t ready_requested;
    FxAllocationObserver index_observer,package_observer,inspect_observer;
    _Atomic uint32_t accounting_ready; /* 0 none, 1 preparing, 2 queued, 3 attached */
    char filter[FX_MANAGER_FILTER_BYTES];
    uint32_t filtered_count,view_slots[FX_MANAGED_ROWS];
    uint32_t search_revision,search_ready;
    uint64_t search_serial_floor;
} FxModuleManager;
FxResult fx_module_manager_init(FxModuleManager *,const FxPlatform *,const FxModuleManagerIO *,int activated);
/* Trusted one-time activation attachment. Contention queues the seed for the
 * current lock holder; it never denies manager IO or activation. */
FxResult fx_module_manager_account(FxModuleManager *,FxAllocationObserver indexes,FxAllocationObserver packages);
/* All filesystem operations run on an owning normal Nikon task, never IRQ. */
FxResult fx_module_manager_scan(FxModuleManager *);
FxResult fx_module_manager_enable(FxModuleManager *,uint32_t slot,uint32_t enabled);
/* Page navigation is copied UI work only; never card IO. */
FxResult fx_module_manager_page(FxModuleManager *,uint32_t page);
/* Case-insensitive ASCII substring of the stable module identity. bytes
 * includes NUL. Empty clears; filtering/paging never reads or renames files.
 * Accepted UI publication may be pending; the normal tick retries it. */
FxResult fx_module_manager_filter(FxModuleManager *,const char *,uint32_t bytes);
FxResult fx_module_manager_freeze(FxModuleManager *);
void fx_module_manager_ready(FxModuleManager *);
void fx_module_manager_tick(FxModuleManager *);
void fx_module_manager_shutdown(FxModuleManager *);
#endif
