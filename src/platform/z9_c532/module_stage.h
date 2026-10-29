#ifndef FX_Z9_C532_MODULE_STAGE_H
#define FX_Z9_C532_MODULE_STAGE_H
#include "card.h"
#include "fenestra_loader.h"
/* One attempt per boot-owned module scope. Called by a normal Nikon worker,
 * never an IRQ, menu callback or private address-space operation. No heap-code
 * execution, table publication, firmware writes or native controller ownership.
 * Digest/capabilities come from copied boot-owned discovery records. File and
 * package may be the exact same temporary buffer; partial overlap is rejected. */
typedef struct {
    FxLoader loader;FxFileCard file;FxZ9CardScope native;
    uint32_t attempted;FxResult result;
    char module_path[48];uint8_t digest[32];
    FxFileObserver file_observer;
} FxZ9ModuleStage;
FxResult fx_z9_module_stage(FxZ9ModuleStage *,const char *native_path,const char *module_path,
                          uint32_t bytes,const uint8_t digest[32],uint64_t capabilities,
                          uint8_t *file_buffer,uint32_t file_capacity,
                          uint8_t *package,uint32_t package_capacity,
                          uint8_t *image,uint32_t image_capacity,uint64_t now);
#endif
