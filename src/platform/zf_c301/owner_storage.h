#ifndef FX_ZF_C301_OWNER_STORAGE_H
#define FX_ZF_C301_OWNER_STORAGE_H
#include "fenestra_native_menu.h"
#include "lifecycle.h"
#include "owner_task.h"
#include "fenestra_hooks.h"
#include "fenestra_native_mapping.h"
#include "fenestra_module_bus.h"
#include "fenestra_native_ui_dispatch.h"
#include "fenestra_native_rtos.h"
#include "ptp.h"
#include "fenestra_native_linux.h"
#include "loaded_modules.h"
#include "apps_domain.h"
#include "ptp_admission.h"
#include "module_stage.h"
#include "services.h"
#include "module_runtime.h"
#include "fenestra_native_modules.h"
#include "address_space.h"
#include "fenestra_shared_accounts.h"
#include "fenestra_platform_native.h"
#include "fenestra_startup_service.h"
#include "fenestra_startup_files.h"
#include "fenestra_native_panic.h"
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
#include "module_controller.h"
#endif

/* Unpublished owner allocation. The native descriptor is an immutable prefix
 * so publishing it need not change stock UI metadata. Publication/context and
 * lifetime are deliberately separate: no native pointer is changed here. */
typedef struct FxZfOwnerStorage {
    FxNativeDescriptor stock;
    uint32_t magic,version;
    uintptr_t page,original_descriptor;
    FxZfLifecycle lifecycle;
    FxZfOwnerTask worker;
    FxHookRegistry hooks;
    FxNativeMappingRegistry mappings;
    FxModuleBus bus;
    FxNativeUiDispatch ui_dispatch;
    _Atomic uint64_t first_fault;
    FxPtp ptp;
    FxPtpIdentity ptp_identity;
    FxRtos rtos;
    FxZfPtpAuthorization ptp_authorization;
    FxLinuxBridge linux;
    FxZfLoadedModules modules;
    FxZfAppsDomain apps_domain;
    FxZfPtpAdmission ptp_admission;
    /* Directory manager is retained with the owner.  It is initialized only
     * when Zf supplies verified directory bindings; an absent manager is a
     * capability boundary, never an empty inventory. */
    FxNativeModules *modules_manager;
    _Atomic uintptr_t menu; /* Optional retained menu generation, published once. */
    FxSharedAccounts accounting;
    FxCapacityRegistry capacity_registry;
    FxNativeCapacityDomains capacity_domains;
    FxStartupService startup;
    FxStartupFiles startup_files;
    _Atomic uint32_t startup_ready;
    FxNativePanicPage *panic;
#if defined(FX_ZF_MODULE_WORKERS) && FX_ZF_MODULE_WORKERS == 1
    FxZfModuleController module_controller;
#endif
#if (defined(FX_ZF_EXPERIMENTAL_MODULE_STAGE_PROBE) && FX_ZF_EXPERIMENTAL_MODULE_STAGE_PROBE == 1) || \
    (defined(FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR) && FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR == 1)
    FxZfModuleRuntime module_runtime;
    FxExecArena module_arena;
    FxZfAddressSpace module_private_space,module_user_space;
    FxZfExecContext module_exec_context;
    FxExecLayout module_layout;
    uintptr_t module_private_root,module_user_root;
    uint8_t *module_image,*module_user_api,*module_user_stack;
    uint64_t *module_private_tables,*module_user_tables;
    uint64_t *module_expected,*module_staging;
    _Alignas(4096) uint8_t module_api[4096];
    _Alignas(4096) uint8_t module_user_exchange_storage[4096];
    FxServiceExchange *module_user_exchange;
    uint8_t *module_user_stack_storage;
    uint32_t module_user_stack_bytes;
    uint8_t *module_file,*module_package;
    uint32_t module_file_capacity,module_package_capacity;
    char module_id[16];
    uint8_t *module_image_raw;
    uint32_t module_image_capacity;
    void *module_tables_raw,*module_plan_raw,*module_user_stack_raw,*module_image_storage_raw;
    uint32_t module_prepare_stage,module_prepare_result,module_publish_stage,module_publish_result;
#endif
} FxZfOwnerStorage;

/* Caller supplies exclusive zeroed writable allocation and stable validated
 * sources. Never call on a published allocation. It becomes retained storage
 * once a native reader can reach it, even when shutdown has completed. */
FxResult fx_zf_owner_storage_prepare(FxZfOwnerStorage *,const FxNativePage *,
                                    const FxNativeDescriptor *);
/* First copied owner/error pair wins. STOP only requests quiescing; it does
 * not release workers, certify clean shutdown, or publish a panic UI. */
void fx_zf_owner_fault(FxZfOwnerStorage *,const FxPlatformOwner *,FxResult);
FxResult fx_zf_startup_prepare(FxZfOwnerStorage *);
void fx_zf_startup_tick(FxZfOwnerStorage *,uint32_t stop);
void fx_zf_startup_before_shutdown(void);
void fx_zf_panic_flush(FxZfOwnerStorage *);
#endif
