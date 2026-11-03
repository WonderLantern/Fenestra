/* C2.01 native GUI/OpenVG and shooting foreground profile. */
#include "fenestra_native_display.h"
#define FN(a) ((uintptr_t)UINT64_C(a))
void *fx_z63_current_page(void) {
    uintptr_t p=(uintptr_t)((void *(*)(void))(uintptr_t)0x403ee214u)();
    for(uint32_t depth=0;depth<16u;++depth) {
        if((p&7u) || p<UINT64_C(0xa00000000) || p>UINT64_C(0xa40000000)-96u)return 0;
        uintptr_t next=*(const uintptr_t *)(p+16u);
        if(!next)return (void *)p;
        if(next==p)return 0;p=next;
    }
    return 0;
}
/* Original foreground callers publish the target state through the graphics
 * owner and rebuild that device's composition list. A raw target-byte update
 * alone is overwritten by the owner and does not publish visible output. */
static void enable_target(uint32_t device,uint32_t kind,uint32_t target,uint32_t enabled) {
    ((void (*)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)0x404060d4u)(device,kind,target,enabled);
    ((void (*)(uint32_t))(uintptr_t)0x40404718u)(device);
}
const FxNativeDisplayProfile fx_native_display_profile={
    .ram_begin=FN(0xa00000000),.ram_end=FN(0xa40000000),
    .current_view_pointer=FN(0xa03393110),
    .native_global_surface=FN(0x403a9418),.global_surface_index=4u,.shooting_view_id=3u,
    .native_global_target=FN(0x40404a08),.gui_surface_initialize=FN(0x40335784),
    .native_graphics_current_device=FN(0x40405898),
    .native_graphics_target_enabled=FN(0x4034ccac),.native_graphics_target_enable=(uintptr_t)enable_target,
    .native_graphics_backing_rectangle=FN(0x4034d0c8),
    .native_view_id=FN(0x403a2da0),.native_view_state=FN(0x403a2d5c),
    .native_menu_progress=FN(0x40403a6c),.native_menu_compositor_submit=FN(0x40403ad0),
    .compound_draw=FN(0x4032f808),.second_compound_draw=FN(0x40334ddc),
    .gui_base_initialize=FN(0x4032a750),.gui_bind_target=FN(0x4032a78c),
    .gui_draw_body=FN(0x40331a4c),.gui_extended_draw_body=FN(0x4033581c),
    .gui_get_visible=FN(0x40405494),.gui_mark_object_dirty=FN(0x40405478),
    .gui_mutex=FN(0xa03427f40),.gui_object_insert=FN(0x404047a8),.gui_object_remove=FN(0x404047b0),
    .gui_set_visible=FN(0x40405488),.gui_target_dimensions=FN(0x4032a7bc),
    .native_constant_paint_rectangle=FN(0x40335c9c),.native_current_page=(uintptr_t)fx_z63_current_page,
    .native_graphics_acquire=FN(0x4032a2dc),.native_graphics_release=FN(0x4032a2e0),
    .native_list_begin=FN(0x40c7867c),.native_list_count=FN(0x40c782f8),.native_list_next=FN(0x40c78464),
    .native_menu_find_row_by_id=FN(0x404047ac),
    .native_mutex_acquire=FN(0x40d328b8),.native_mutex_release=FN(0x40d32864),
    .native_object_dispatch=FN(0x40332240),.native_rgba_float_conversion=FN(0x402f1148),
    .native_transparent_clear_rectangle=FN(0x403364fc),.row_draw=FN(0x4032b3dc),.row_children_offset=FN(0x128),
    .vgAppendPathData=FN(0x40250044),.vgClear=FN(0x4024fccc),.vgClearPath=FN(0x4024fdec),
    .vgCreatePaint=FN(0x40250798),.vgCreatePath=FN(0x4024fd48),.vgDestroyPaint=FN(0x40250804),
    .vgDestroyPath=FN(0x4024fe54),.vgDrawPath=FN(0x40250700),.vgGetError=FN(0x4024e704),
    .vgGetPaint=FN(0x40250924),.vgGetfv=FN(0x4024e9cc),.vgGeti=FN(0x4024e96c),
    .vgSetPaint=FN(0x40250870),.vgSetParameterfv=FN(0x4024eddc),.vgSetParameteri=FN(0x4024ec70),
    .vgSetfv=FN(0x4024e80c),.vgSeti=FN(0x4024e7ac),
    .vgCreateImage=FN(0x40250af8),.vgDestroyImage=FN(0x40250c14),
    .vgImageSubData=FN(0x40250d40),.vgDrawImage=FN(0x40251258),
    .vgLoadMatrix=FN(0x4024f8a4),.vgGetMatrix=FN(0x4024f8fc),
    .vgGetPixels=FN(0x402514e8),.vgSetPixels=FN(0x402512ec),
};
