#include "fenestra_settings.h"
#include "fenestra_memory.h"

const char fx_settings_title[] = "Fenestra / WonderLantern";

static int valid(const FxSettings *s) { return s != NULL && s->abi == FX_ABI; }
static void synchronize(FxSettings *s) {
    if (s->extension == NULL) return;
    switch (s->extension->loader.runtime.phase) {
        case FX_ACTIVE: s->stage = FX_PAGE_ACTIVE; break;
        case FX_QUIESCING: s->stage = FX_PAGE_QUIESCING; break;
        case FX_DISABLED: s->stage = FX_PAGE_DISABLED; break;
        case FX_PANIC: s->stage = FX_PAGE_PANIC; break;
        default: s->stage = FX_PAGE_LOADING; break;
    }
    if (s->stage != FX_PAGE_ACTIVE) fx_debug_enable(&s->extension->debug, 0);
}
void fx_settings_init(FxSettings *s) {
    if (s == NULL) return;
    memset(s, 0, sizeof(*s)); s->abi = FX_ABI; s->stage = FX_PAGE_DORMANT;
}
void fx_settings_view(const FxSettings *s, FxSettingsView *v) {
    static const char *const names[] = {"Inactive", "Activation queued", "Loading", "Active",
                                        "Stopping", "Disabled - restart required", "Panic - restart required"};
    if (v == NULL) return;
    memset(v, 0, sizeof(*v)); v->title = fx_settings_title; v->parent = "Setup menu";
    if (!valid(s) || (unsigned)s->stage > (unsigned)FX_PAGE_PANIC) { v->status = "Unavailable"; return; }
    v->status = names[s->stage]; v->last_result = s->last_result;
    v->activate_enabled = s->stage == FX_PAGE_DORMANT && !s->consumed_activation;
    v->deactivate_enabled = s->stage == FX_PAGE_QUEUED || s->stage == FX_PAGE_LOADING || s->stage == FX_PAGE_ACTIVE;
    v->usb_on_enabled = s->stage == FX_PAGE_ACTIVE;
    v->reboot_required = s->stage == FX_PAGE_DISABLED || s->stage == FX_PAGE_PANIC;
    if (s->extension != NULL) {
        v->generation = s->extension->loader.runtime.generation;
        v->usb_enabled = s->extension->debug.enabled;
        v->usb_on_enabled = v->usb_on_enabled && !v->usb_enabled;
        v->usb_off_enabled = v->usb_enabled;
    }
}
FxResult fx_settings_choose(FxSettings *s, FxUiAction action, uint64_t now, uint64_t timeout) {
    FxResult result;
    if (!valid(s)) return FX_ARGUMENT;
    if (action == FX_UI_ACTIVATE) {
        if (s->stage != FX_PAGE_DORMANT || s->consumed_activation) return FX_STATE;
        s->consumed_activation = 1; s->stage = FX_PAGE_QUEUED;
        return FX_OK;
    }
    if (action == FX_UI_DEACTIVATE) {
        if (s->stage == FX_PAGE_QUEUED) { s->stage = FX_PAGE_DISABLED; return FX_OK; }
        if (s->extension == NULL) return FX_BUSY; /* worker owns pending device cleanup */
        result = fx_disable(&s->extension->loader.runtime, now, timeout);
        s->last_result = result; synchronize(s); return result;
    }
    if (action != FX_UI_USB_ON && action != FX_UI_USB_OFF) return FX_ARGUMENT;
    if (s->extension == NULL || (action == FX_UI_USB_ON && s->stage != FX_PAGE_ACTIVE)) return FX_STATE;
    fx_debug_enable(&s->extension->debug, action == FX_UI_USB_ON); return FX_OK;
}
FxResult fx_settings_take_activation(FxSettings *s) {
    if (!valid(s)) return FX_ARGUMENT;
    if (s->stage != FX_PAGE_QUEUED || !s->consumed_activation) return FX_STATE;
    s->stage = FX_PAGE_LOADING; return FX_OK;
}
FxResult fx_settings_load(FxSettings *s, FxExtension *extension, const FxActivation *a) {
    FxResult result;
    if (!valid(s) || extension == NULL || a == NULL) return FX_ARGUMENT;
    if (s->stage != FX_PAGE_LOADING || !s->consumed_activation || s->extension != NULL) return FX_STATE;
    s->extension = extension;
    fx_loader_init(&extension->loader, a->verified_capabilities, a->ram_budget);
    fx_debug_init(&extension->debug, &extension->loader);
    result = fx_loader_prepare(&extension->loader, a->io, a->path, a->package_bytes,
                               a->package_sha256, a->firmware_sha256, a->package, a->package_capacity,
                               a->image, a->image_capacity, a->now, a->timeout);
    /* Bad worker arguments also consume this cold attempt without touching IO. */
    if (result != FX_OK && extension->loader.runtime.phase == FX_BYPASS) {
        (void)fx_begin(&extension->loader.runtime, 0u, 0u, a->now, 0u);
    }
    s->last_result = result; synchronize(s); return result;
}
FxResult fx_settings_publish(FxSettings *s, uint64_t now) {
    FxResult result;
    if (!valid(s)) return FX_ARGUMENT;
    if (s->stage != FX_PAGE_LOADING || s->extension == NULL) return FX_STATE;
    result = fx_loader_activate(&s->extension->loader, now);
    s->last_result = result; synchronize(s); return result;
}
FxResult fx_settings_publish_mapped(FxSettings *s, FxExecArena *arena, uint64_t now) {
    FxResult result;
    if (!valid(s) || arena == NULL) return FX_ARGUMENT;
    if (s->stage != FX_PAGE_LOADING || s->extension == NULL) return FX_STATE;
    result = fx_loader_activate_mapped(&s->extension->loader, arena, now);
    s->last_result = result; synchronize(s); return result;
}
FxResult fx_settings_storage_failed(FxSettings *s, FxResult result, int closed) {
    if (!valid(s) || result <= FX_OK || result > FX_EMPTY) return FX_ARGUMENT;
    if (s->stage != FX_PAGE_LOADING || s->extension != NULL) return FX_STATE;
    s->stage = closed ? FX_PAGE_DISABLED : FX_PAGE_PANIC;
    s->last_result = result; return result;
}
FxResult fx_settings_tick(FxSettings *s, uint64_t now) {
    FxResult result;
    if (!valid(s)) return FX_ARGUMENT;
    if (s->extension == NULL) return FX_OK;
    result = fx_poll(&s->extension->loader.runtime, now);
    if (result != FX_OK) s->last_result = result;
    synchronize(s); return result;
}
FxResult fx_settings_confirm_stopped(FxSettings *s, uint64_t now) {
    FxResult result;
    if (!valid(s)) return FX_ARGUMENT;
    if (s->stage != FX_PAGE_QUIESCING || s->extension == NULL) return FX_STATE;
    result = fx_loader_confirm_quiesced(&s->extension->loader,
                                        s->extension->loader.runtime.generation, now);
    s->last_result = result; synchronize(s); return result;
}
