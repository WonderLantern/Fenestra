#ifndef FENESTRA_NATIVE_DISPLAY_H
#define FENESTRA_NATIVE_DISPLAY_H
#include "fenestra_demo.h"
#include "fenestra.h"
#include "fenestra_native_menu.h"
#include <stdatomic.h>
/* Original native object/list ABI. All mutable data is retained owned RAM;
 * addresses and native callback classes come from one checked target profile. */
typedef struct {
    uintptr_t compound_draw;
    uintptr_t current_view_pointer;
    uintptr_t ram_begin,ram_end,native_menu_progress;
    uintptr_t gui_base_initialize;
    uintptr_t gui_bind_target;
    uintptr_t gui_draw_body;
    uintptr_t gui_extended_draw_body;
    uintptr_t gui_get_visible;
    uintptr_t gui_mark_object_dirty;
    uintptr_t gui_mutex;
    uintptr_t gui_object_insert;
    uintptr_t gui_object_remove;
    uintptr_t gui_set_visible;
    uintptr_t gui_target_dimensions;
    uintptr_t native_constant_paint_rectangle;
    uintptr_t native_current_page;
    uintptr_t native_global_surface;
    uintptr_t global_surface_index,shooting_view_id,native_global_target;
    uintptr_t gui_surface_initialize,native_graphics_current_device;
    uintptr_t native_graphics_target_enabled,native_graphics_target_enable;
    uintptr_t native_graphics_acquire;
    uintptr_t native_graphics_backing_rectangle;
    uintptr_t native_graphics_release;
    uintptr_t native_list_begin;
    uintptr_t native_list_count;
    uintptr_t native_list_next;
    uintptr_t native_menu_compositor_submit;
    uintptr_t native_menu_find_row_by_id;
    uintptr_t native_mutex_acquire;
    uintptr_t native_mutex_release;
    uintptr_t native_object_dispatch;
    uintptr_t native_rgba_float_conversion;
    uintptr_t native_transparent_clear_rectangle;
    uintptr_t native_view_id;
    uintptr_t native_view_state;
    uintptr_t row_draw;
    uintptr_t row_children_offset;
    uintptr_t second_compound_draw;
    uintptr_t vgAppendPathData;
    uintptr_t vgClear;
    uintptr_t vgClearPath;
    uintptr_t vgCreatePaint;
    uintptr_t vgCreatePath;
    uintptr_t vgDestroyPaint;
    uintptr_t vgDestroyPath;
    uintptr_t vgDrawPath;
    uintptr_t vgGetError;
    uintptr_t vgGetPaint;
    uintptr_t vgGetfv;
    uintptr_t vgGeti;
    uintptr_t vgSetPaint;
    uintptr_t vgSetParameterfv;
    uintptr_t vgSetParameteri;
    uintptr_t vgSetfv;
    uintptr_t vgSeti;
    uintptr_t vgCreateImage,vgDestroyImage,vgImageSubData,vgDrawImage;
    uintptr_t vgLoadMatrix,vgGetMatrix;
    uintptr_t vgGetPixels,vgSetPixels;
} FxNativeDisplayProfile;
/* Owned modal page only, on the original GUI task. Capture precedes first
 * native paint; restore does not redraw a hidden logical parent. */
FxResult fx_native_backdrop_open(FxNativePage *);
FxResult fx_native_backdrop_restore(const FxNativePage *);
/* Caller owns GUI exclusion. Restore a modal backdrop or repaint an ordinary
 * retained page tree, including native row/icon children and surface roots. */
FxResult fx_native_menu_restore(const FxNativePage *);
void fx_native_backdrop_close(FxNativePage *);
extern const FxNativeDisplayProfile fx_native_display_profile;
typedef struct {
    _Alignas(8) uint8_t object[0xc0];
    void *page,*surface;
    const FxDemoScene *scene;
    uint32_t width,height;
    _Atomic uint32_t attached;
    uint32_t frames;
    uint32_t callbacks,rectangles,error,restores;
    /* Bounded driver transactions, independent of frame/scene size. */
    uint8_t segments[128u*5u];
    uint32_t coordinates[128u*8u]; /* IEEE-754 bits; no task-side FP arithmetic */
    uint64_t original_objects[32][4];
    uint32_t original_object_count;
    uintptr_t application;
    uint32_t global_overlay,original_visible;
    uint16_t viewport[4]; /* Native source rectangle: left, top, right, bottom. */
    _Atomic uint64_t *gate; /* Optional epoch/enable gate for generic scenes. */
    _Atomic uint32_t readers;
    uint32_t exact_colors;
    uint32_t target[3],target_was_enabled;
} FxNativeDisplay;
typedef struct { uintptr_t address;uint32_t id,state; } FxNativeView;

FxNativeView fx_native_current_view(void);
uint32_t fx_native_display_capabilities(void);
uint32_t fx_native_display_global_ready(void);
uint32_t fx_native_display_is_current(const FxNativeDisplay *);
FxResult fx_native_display_attach_global(FxNativeDisplay *,const FxDemoScene *);
FxResult fx_native_display_attach(FxNativeDisplay *,void *,const FxDemoScene *);
FxResult fx_native_display_present(FxNativeDisplay *,const FxDemoScene *);
void fx_native_display_detach(FxNativeDisplay *);
void *fx_native_display_current_page(uint32_t expected_id);
#endif
