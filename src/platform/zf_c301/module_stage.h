#ifndef FX_ZF_C301_MODULE_STAGE_H
#define FX_ZF_C301_MODULE_STAGE_H
#include "card.h"
#include "fenestra_loader.h"
/* One bounded attempt on a normal task. Copies/validates a module only;
 * never maps executable memory, invokes entry or publishes an application. */
typedef struct {
    FxLoader loader;
    FxFileCard file;
    FxZfCardScope native;
    uint32_t attempted;
    FxResult result;
    char module_path[48];
    uint8_t digest[32];
    uint8_t snapshot_digest[32];
    FxCardReceipt card_receipt;
    uint32_t runtime_phase;
    FxFileObserver file_observer;
} FxZfModuleStage;
FxResult fx_zf_module_stage(FxZfModuleStage *,const FxZfCardNative *,const char *native_path,
    const char *logical_path,uint32_t bytes,const uint8_t digest[32],uint64_t capabilities,
    uint8_t *file,uint32_t file_capacity,uint8_t *package,uint32_t package_capacity,
    uint8_t *image,uint32_t image_capacity,uint64_t now);
#endif
