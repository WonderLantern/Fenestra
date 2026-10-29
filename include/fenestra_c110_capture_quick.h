#ifndef FENESTRA_C110_CAPTURE_QUICK_H
#define FENESTRA_C110_CAPTURE_QUICK_H
#include <stdint.h>
#include <stdatomic.h>
#include "fenestra_video_configuration.h"

/* Native UI descriptions copied into owned RAM. No ROM/NVM table mutation. */
typedef struct { uint64_t data,flags; } FxC110QuickEntry;
typedef struct { uint64_t word[5]; } FxC110QuickVariant;
typedef struct { uint64_t id,data,count,cells[4]; } FxC110QuickWidget;
typedef struct { uint32_t index,kind,resource,reserved[3]; } FxC110QuickValue;
typedef struct { uint32_t id,kind; uint64_t entries,count; } FxC110QuickControl;
typedef struct {
    FxC110QuickControl control;
    FxC110QuickEntry entries[63];
    FxC110QuickVariant variants[17u+FX_VIDEO_MAX_MODES][2],caption_variant;
    FxC110QuickWidget widgets[17u+FX_VIDEO_MAX_MODES][2],caption;
    FxC110QuickValue icons[2][17u+FX_VIDEO_MAX_MODES],captions[17u+FX_VIDEO_MAX_MODES];
    uint64_t cells[17u+FX_VIDEO_MAX_MODES][2][4],caption_cells[4];
    uint32_t prepared,initializing,selecting,staged;
    _Atomic uint32_t pending,reinitialize;
    _Atomic uint32_t desired_mode;
    uint32_t previous_mode,revision,codec,family,rate,operation;
} FxC110CaptureQuick;
uint64_t fx_c110_capture_quick_template(uint32_t,uint32_t);
uint64_t fx_c110_capture_quick_control(uint32_t);
uint64_t fx_c110_capture_quick_initialize(uint32_t);
uint64_t fx_c110_capture_quick_current(void);
uint64_t fx_c110_capture_quick_commit(void);
uint64_t fx_c110_capture_deferred(const uint32_t *,const uint32_t *,uint32_t);
int fx_c110_capture_quick_handoff(void);
int fx_c110_capture_quick_prepare(void);
int fx_c110_capture_quick_display_mode(uint32_t *mode);
#endif
