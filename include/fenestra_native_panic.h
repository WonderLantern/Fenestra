#ifndef FENESTRA_NATIVE_PANIC_H
#define FENESTRA_NATIVE_PANIC_H
#include "fenestra_native_display.h"
#include "fenestra_panic.h"
/* One core page per camera activation, never embedded in every module worker.
 * Allocate before starting modules; retain while native draw references exist. */
typedef struct {
    FxNativeDisplay display;
    FxScene scene;
    FxSceneCommand commands[FX_PANIC_COMMANDS];
    FxPanicReport report;
    uint32_t captured,shown,result;
    _Atomic uint64_t render_gate;
} FxNativePanicPage;
/* Initialize fresh, unpublished storage only. Never reset a published page. */
static inline void fx_native_panic_page_init(FxNativePanicPage *page) {
    *page=(FxNativePanicPage){0};
    page->scene=(FxScene){.width=1280u,.height=800u,.capacity=FX_PANIC_COMMANDS,.commands=page->commands};
    atomic_store(&page->render_gate,1u);page->display.gate=&page->render_gate;
}
/* Both arguments are retained until reboot; retrying a failed submission
 * never modifies a scene that a late native draw callback could still read. */
FxResult fx_native_panic_show(FxNativeDisplay *,FxScene *,const FxPanicReport *);
/* Retained controller task, after capturing the first report. Rebind after
 * native menu/application transitions; do not repaint an unchanged target. */
FxResult fx_native_panic_poll(FxNativePanicPage *);
#endif
