#ifndef FX_ZF_MODULE_CONTROLLER_H
#define FX_ZF_MODULE_CONTROLLER_H
#include "module_worker.h"
#include "services.h"
enum {FX_ZF_LOAD_NONE,FX_ZF_LOAD_DISCOVERY,FX_ZF_LOAD_DIRECTORY_OPEN,
    FX_ZF_LOAD_DIRECTORY_NEXT,FX_ZF_LOAD_DIRECTORY_ENTRY,FX_ZF_LOAD_PACKAGE_READ,
    FX_ZF_LOAD_PACKAGE_LENGTH,FX_ZF_LOAD_PACKAGE_CHECK,FX_ZF_LOAD_DIRECTORY_CLOSE,
    FX_ZF_LOAD_WORKER_SLOTS,FX_ZF_LOAD_LAYOUT,FX_ZF_LOAD_ALLOCATE,
    FX_ZF_LOAD_SERVICES,FX_ZF_LOAD_WORKER_START,FX_ZF_LOAD_WORKER_WAIT,
    FX_ZF_LOAD_WORKER_PANIC};
typedef struct {
    FxModuleInventory inventory;
    FxZfModuleWorker **workers;
    FxZfServices **services;
    _Atomic uint32_t count;
    uint32_t capacity,next,phase;
    FxResult error;
    /* Owner-task-only progress, frozen before publishing first_fault. */
    uint32_t stage,fault_stage;
    FxModuleDirectory source_directory;
    uint64_t directory_bytes;
    uint32_t read_bytes;
    /* Copied public package-field diagnosis; never a pointer into read scratch. */
    char check_detail[60];
    uint8_t scratch[16385];
} FxZfModuleController;
const char *fx_zf_modules_fault_stage(const FxZfModuleController *);
struct FxZfOwnerStorage;
FxResult fx_zf_modules_step(struct FxZfOwnerStorage *,uint32_t stop);
FxResult fx_zf_modules_action(FxZfModuleController *,FxPlatformOwner *,uint32_t);
#endif
