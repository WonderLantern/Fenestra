#ifndef FENESTRA_SETTINGS_H
#define FENESTRA_SETTINGS_H
#include "fenestra_debug.h"

typedef enum {
    FX_PAGE_DORMANT, FX_PAGE_QUEUED, FX_PAGE_LOADING, FX_PAGE_ACTIVE,
    FX_PAGE_QUIESCING, FX_PAGE_DISABLED, FX_PAGE_PANIC
} FxPageStage;
typedef enum { FX_UI_ACTIVATE = 1, FX_UI_DEACTIVATE, FX_UI_USB_ON, FX_UI_USB_OFF } FxUiAction;
typedef struct { FxLoader loader; FxDebug debug; } FxExtension;

/* Permanent settings stub: no buffers, device handles or executable image.
 * Initialize once for a cold instance, never on page-open or USB-reset. */
typedef struct {
    FxExtension *extension; /* NULL until the explicit Activate worker runs */
    FxPageStage stage;
    FxResult last_result;
    uint32_t abi, consumed_activation;
} FxSettings;
typedef struct {
    const char *title, *parent, *status;
    uint32_t activate_enabled, deactivate_enabled, usb_on_enabled, usb_off_enabled;
    uint32_t usb_enabled, reboot_required, generation;
    FxResult last_result;
} FxSettingsView;
typedef struct {
    const FxCardIO *io;
    const char *path;
    uint32_t package_bytes;
    const uint8_t *package_sha256, *firmware_sha256;
    uint8_t *package, *image;
    uint32_t package_capacity, image_capacity;
    uint64_t verified_capabilities, ram_budget, now, timeout;
} FxActivation;

extern const char fx_settings_title[];
void fx_settings_init(FxSettings *);
void fx_settings_view(const FxSettings *, FxSettingsView *);
/* Only the camera settings event owner calls this. Activate queues work and
 * returns immediately; it never opens a card or runs a module in a GUI call. */
FxResult fx_settings_choose(FxSettings *, FxUiAction, uint64_t now, uint64_t stop_timeout);
FxResult fx_settings_take_activation(FxSettings *);
FxResult fx_settings_load(FxSettings *, FxExtension *, const FxActivation *);
FxResult fx_settings_publish(FxSettings *, uint64_t now);
FxResult fx_settings_publish_mapped(FxSettings *, FxExecArena *, uint64_t now);
FxResult fx_settings_storage_failed(FxSettings *, FxResult, int cleanup_confirmed);
FxResult fx_settings_tick(FxSettings *, uint64_t now);
FxResult fx_settings_confirm_stopped(FxSettings *, uint64_t now);
#endif
