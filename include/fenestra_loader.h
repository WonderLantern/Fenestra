#ifndef FENESTRA_LOADER_H
#define FENESTRA_LOADER_H
#include "fenestra_module.h"
#include "fenestra_api.h"
#include "fenestra_exec.h"

typedef struct {
    FxRuntime runtime;
    FxCardReceipt card;
    FxModule module;
    uint8_t package_sha256[32], target_firmware_sha256[32];
    uint8_t *image;
    FxExecArena *exec_arena;
    uint32_t source_reserved; /* Temporary package accounting; no package pointer escapes. */
} FxLoader;

void fx_loader_init(FxLoader *, uint64_t verified_capabilities, uint64_t private_ram_budget);
FxResult fx_loader_prepare(FxLoader *, const FxCardIO *, const char *path,
                          uint32_t package_bytes, const uint8_t package_sha256[32],
                          const uint8_t firmware_sha256[32], uint8_t *package_buffer,
                          uint32_t package_capacity, uint8_t *image_buffer,
                          uint32_t image_capacity, uint64_t now, uint64_t timeout);
/* Bare-metal activation checks EL/MMU state and cache visibility. Mapped
 * activation uses a separately bound private EL1 arena with page permissions.
 * Neither backend infers executable heap permissions or changes stock tables
 * without a verified owner supplying a private arena. */
/* Trusted owner reports that verified source scratch was actually released,
 * before publication. Keeps digest/card/descriptor receipts; no deallocation. */
FxResult fx_loader_source_released(FxLoader *);
FxResult fx_loader_activate(FxLoader *, uint64_t now);
FxResult fx_loader_activate_mapped(FxLoader *, FxExecArena *, uint64_t now);
FxResult fx_loader_invoke(FxLoader *, uint32_t generation, const FxModuleServices *,
                         uint64_t now, int *module_result);
/* Platform stop acknowledgment: mapped images are revoked before the runtime
 * can release reservations. In-flight mapped calls return Busy, not unmap. */
FxResult fx_loader_confirm_quiesced(FxLoader *, uint32_t generation, uint64_t now);
int fx_loader_release_allowed(const FxLoader *);
#endif
