#ifndef FENESTRA_DEMO_H
#define FENESTRA_DEMO_H
#include "fenestra_api.h"
#include "fenestra_canvas.h"
#include "fenestra_scene.h"
#define FX_DEMO_COLUMNS 100u
#define FX_DEMO_ROWS 38u
#define FX_DEMO_CELLS (FX_DEMO_COLUMNS*FX_DEMO_ROWS)
#define FX_DEMO_CYCLE 480u
/* Device-independent scene commands. No pixel format, DMA or panel address
 * crosses the module boundary. A platform renderer consumes them after the
 * module returns and the original firmware address space is restored. */
enum { FX_DEMO_CLEAR=FX_SCENE_CLEAR, FX_DEMO_GLYPH=FX_SCENE_GLYPH };
typedef FxSceneCommand FxDemoDraw;
typedef FxScene FxDemoScene;
#define FX_DEMO_DRAW_CAPACITY (FX_DEMO_CELLS+64u)
typedef struct {
    uint32_t frame, cell_count, target_count, locked_count, cycles;
    uint64_t cell_pool_address;
} FxDemoStats;
/* Optional service suffix. Existing ABI-1 modules still use the base prefix.
 * One invocation renders one frame and returns; no permanent lease or loop. */
typedef struct {
    FxModuleServices base;
    FxCanvas *canvas;
    uint32_t frame, reserved;
    FxDemoStats *stats;
    FxDemoScene *scene;
} FxDemoServices;
#endif
