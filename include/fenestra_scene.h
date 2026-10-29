#ifndef FENESTRA_SCENE_H
#define FENESTRA_SCENE_H
#include "fenestra_platform.h"
#define FX_SCENE_ABI 1u
#define FX_SCENE_COMMANDS 4096u
#define FX_SCENE_EXTENT 4096u
#define FX_SCENE_DATA_BYTES (4u*1024u*1024u)
#define FX_SCENE_GLOBAL_PAGE UINT32_MAX
enum {FX_SCENE_CAP_PAGE=1u,FX_SCENE_CAP_GLOBAL=2u,FX_SCENE_CAP_IMAGE=4u,FX_SCENE_CAP_CAPACITY=8u};
enum {FX_SCENE_CLEAR=1u,FX_SCENE_GLYPH,FX_SCENE_RECT,FX_SCENE_IMAGE};
enum {FX_SCENE_QUERY,FX_SCENE_SUBMIT,FX_SCENE_HIDE,FX_SCENE_STATUS,FX_SCENE_CAPACITY};
/* RGB888. GLYPH uses ASCII glyph and integer scale. RECT reuses glyph as
 * width and scale as height. SDK constructors hide that compact wire union. */
typedef struct {uint16_t kind,x,y,glyph;uint32_t scale,rgb;} FxSceneCommand;
/* IMAGE uses glyph/scale for destination width/height and rgb for the offset
 * of an FxSceneImage descriptor in the submitted data. Pixels are top-down,
 * non-premultiplied sRGB RGBA bytes; stride may include padding. */
enum {FX_SCENE_RGBA8=1u};
typedef struct {uint32_t width,height,stride,format,pixels,reserved;} FxSceneImage;
typedef struct {uint32_t width,height,capacity,count;FxSceneCommand *commands;
    uint8_t *data;uint32_t data_bytes,data_capacity;} FxScene;
typedef struct {uint32_t abi,bytes,page,width,height,count;uint64_t commands;} FxSceneSubmission;
/* Optional extension: base.bytes is sizeof(FxSceneImageSubmission), ABI stays
 * 1. Query IMAGE capability before using it. Old 32-byte submissions remain. */
typedef struct {FxSceneSubmission base;uint64_t data;uint32_t data_bytes,reserved;} FxSceneImageSubmission;
typedef struct {uint32_t abi,bytes,max_commands,max_extent,commands,format,contexts,capabilities;} FxSceneInfo;
/* Requested core-owned scene storage, not Nikon graphics allocations or free RAM.
 * contexts in FxSceneInfo is a budget-derived theoretical bound, not free slots. */
enum {FX_SCENE_CAPACITY_PARTIAL=1u};
typedef struct {uint32_t abi,bytes,flags,reserved;
    uint64_t limit,used,peak,contexts,published,context_bytes;} FxSceneCapacity;
typedef struct {FxScene scene[2];uint32_t front,staged,ready,frames;} FxSceneStore;
void fx_scene_init(FxSceneStore *,FxSceneCommand *first,FxSceneCommand *second);
FxResult fx_scene_validate(const FxScene *);
FxResult fx_scene_stage(FxSceneStore *,FxPlatformOwner *,const FxSceneSubmission *);
FxResult fx_scene_stage_images(FxSceneStore *,FxPlatformOwner *,const FxSceneImageSubmission *);
const FxScene *fx_scene_candidate(const FxSceneStore *);
void fx_scene_commit(FxSceneStore *);
#endif
