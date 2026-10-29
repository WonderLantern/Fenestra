#ifndef FENESTRA_C110_CAPTURE_MENU_H
#define FENESTRA_C110_CAPTURE_MENU_H
#include "fenestra_c110_capture_hooks.h"
#include "fenestra_platform.h"
#include "fenestra_c110_capture_quick.h"
#include "fenestra_allocation_observer.h"

enum { FX_C110_CAPTURE_TEXT0=3505u,FX_C110_CAPTURE_TEXT1=3506u,
       FX_C110_CAPTURE_ROW0=0x7fa0u,FX_C110_CAPTURE_ROW1=0x7fa1u };
typedef struct { uint32_t word[11]; } FxC110CaptureRow;
typedef struct { uint64_t event,callback,condition,next; } FxC110CaptureEvent;
typedef struct {
    uint32_t flags,reserved;
    uint64_t header,rows,events;
    uint16_t count,geometry[15];
} FxC110CaptureDescriptor;
typedef struct {
    FxC110CaptureHooks hooks;
    _Atomic uintptr_t owner;
    _Atomic uint32_t gate,revision,ready,stopping,stop_ack,selecting;
    FxC110CaptureDescriptor descriptor;
    FxC110CaptureRow rows[109u+FX_VIDEO_MAX_MODES];
    uint64_t events[109u+FX_VIDEO_MAX_MODES];
    FxC110CaptureEvent custom_events[FX_VIDEO_MAX_MODES][16];
    uint64_t callbacks[5];
    uint32_t menu_attached,last_result,uploaded,upload_bytes,configuration_capacity;
    uint16_t labels[FX_VIDEO_MAX_MODES][48];
    FxC110CaptureQuick quick;
    /* Shared native adapter and its upload outlive any service handle. */
    FxAllocationObserver allocation_observer;
    uint32_t configuration_accounted;
} FxC110CaptureMenu;
/* Owned native State accessor, never a module-supplied pointer. */
FxC110CaptureMenu *fx_c110_capture_menu(void);
FxC110CaptureMenu *fx_c110_capture_create(void); /* Normal activated worker only. */
void fx_c110_capture_menu_attach(void *);
uint64_t fx_c110_capture_page_init(void *);
uint64_t fx_c110_capture_resolution(void *);
uint64_t fx_c110_capture_lookup(const void *,uint32_t,uint32_t,uint32_t,uint32_t);
uint64_t fx_c110_capture_initialize(const void *);
uint64_t fx_c110_capture_restart(const void *);
uint64_t fx_c110_capture_build(const void *);
uint64_t fx_c110_capture_job(const void *);
uint64_t fx_c110_capture_properties(const uint32_t *,const uint32_t *,uint32_t);
uint64_t fx_c110_capture_property(uint32_t,uint32_t);
const FxVideoConfigurationMode *fx_c110_capture_mode(const FxC110CaptureMenu *,uint32_t);
FxResult fx_c110_capture_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);
void fx_c110_capture_stop(void);
uint32_t fx_c110_capture_poll(void);
int fx_c110_capture_stopped(void);
#endif
