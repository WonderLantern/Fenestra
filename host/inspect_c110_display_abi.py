"""Pin the C1.10 native GUI/OpenVG bindings to independent ROM evidence."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BASE=0x40160000
SOURCE_SHA='7d81293b5a793f034f15c31554e0ca7b552847da8d15055eec33541c3a509957'
FUNCTIONS={
    'firmware_heap_allocate':0x40f41084,
    'native_mutex_acquire':0x40f40c30,'native_mutex_release':0x40f40bdc,
    'gui_base_initialize':0x4032a554,'gui_bind_target':0x4032a590,
    'gui_target_dimensions':0x4032a5c0,'gui_object_insert':0x4040032c,
    'gui_object_remove':0x40400334,'gui_mark_all_dirty':0x40400408,
    'gui_draw_locked_wrapper':0x40400358,'gui_draw_body':0x40331e74,
    'gui_extended_draw_body':0x40335d6c,
    'native_constant_paint_rectangle':0x40336200,'native_current_page':0x403e8e8c,
    'native_object_dispatch':0x40332694,
    'native_transparent_clear_rectangle':0x40336adc,
    'gui_mark_object_dirty':0x40401060,
    'native_global_surface':0x403a3f0c,
    'native_view_id':0x4039dca4,'native_view_state':0x4039dc60,
    'gui_set_visible':0x40401070,'gui_get_visible':0x4040107c,
    'native_graphics_logical_rectangle':0x4034d770,
    'native_graphics_backing_rectangle':0x4034d85c,
    'native_list_count':0x40e7aaa4,'native_list_begin':0x40e7ae54,'native_list_next':0x40e7ac10,
    'native_menu_find_row_by_id':0x40400330,'native_text_cache_release':0x40337eec,
    'native_object_disabled':0x40401090,'native_object_content':0x404010d0,
    'native_row_child_lookup':0x404012d0,
    'native_menu_compositor_submit':0x403ff6b0,'native_menu_progress':0x403ff64c,
    'native_graphics_acquire':0x4032a0c4,'native_graphics_release':0x4032a0c8,
    'vgCreatePath':0x40252f0c,'vgAppendPathData':0x4025321c,
    'vgDrawPath':0x402538fc,'vgClearPath':0x40252fb0,'vgDestroyPath':0x4025301c,
    'vgCreatePaint':0x40253998,'vgSetParameteri':0x40251db8,
    'vgSetParameterfv':0x40251f34,'vgSetPaint':0x40253a74,
    'vgGetPaint':0x40253b2c,'vgDestroyPaint':0x40253a04,
    'vgGetError':0x402517fc,'native_rgba_float_conversion':0x402f380c,
    'vgSeti':0x402518a8,'vgGeti':0x40251a84,
    'vgSetfv':0x40251908,'vgGetfv':0x40251ae8,'vgClear':0x40252e8c,
}
CALLER_RANGES={
    'menu_object_insertion_and_drawing':(0x403bc830,0xa0),
    'native_menu_draw_then_compositor':(0x403b6edc,0x48),
    'independent_menu_draw_then_compositor':(0x4060ad58,0x44),
    'gui_lock_and_dispatch':(0x40400358,0x94),
    'Nikon_OpenVG_path_paint_draw_destroy':(0x402f3454,0xf4),
    'native_VGU_rect_append':(0x4026062c,0xa8),
    'clear_path_resets_counts_and_sets_capabilities':(0x4025c4fc,0x1c),
    'compound_menu_text_child_list':(0x4032fd98,0x38),
    'extended_draw_dispatches_b0_or_48':(0x40335dd8,0x64),
    'row_draw_walks_130_child_list':(0x4032b300,0x44),
    'native_row_lookup':(0x40331f54,0x90),
    'text_cache_getter_load':(0x40337c04,0x48),
    'native_row_value_creation':(0x403b5714,0x180),
    'native_enabled_attribute':(0x4032a294,0x50),
    'native_page_open_callback':(0x403e88cc,0x18),
    'category_bar_reuses_native_image_and_delegated_draw':(0x403b6a58,0x10c),
    'native_dirty_flag_setter':(0x40401060,0x10),
    'native_page_parent_chain_and_redraw':(0x403e8828,0x60),
    'native_object_dispatch_honors_extended_callback':(0x40332694,0x78),
    'native_page_selects_primary_or_overlay_foreground':(0x403bdab4,0x8c),
    'native_popup_selects_background_surface3':(0x403b6ccc,0x38),
    'native_menu_changes_surface_visibility':(0x40529ac8,0x2c),
    'native_compositor_uses_source_and_destination_rectangles':(0x40338e50,0x60),
}


def inspect(original=None):
    if original is None:raise ValueError('explicit original application bytes required')
    from platform_profiles import PROFILES
    digest=hashlib.sha256(original).hexdigest()
    if digest not in (SOURCE_SHA,PROFILES['zr-c110']['sha256']):raise ValueError('C1.10 ROM pin differs')
    def window(address,size):
        start=address-BASE
        if not 0<=start<0x16e7000-size:raise ValueError('outside C1.10 executable text')
        data=original[start:start+size]
        return {'address':hex(address),'bytes':size,'hex':data.hex(),'sha256':hashlib.sha256(data).hexdigest()}
    return {'target':'Nikon ZR C1.10','rom_sha256':digest,
        'scope':'offline original native instructions; physical calling contract remains unqualified',
        'public_Nikon_SDK':False,'binding_origin':'C1.10 native callers; Z7 addresses not reused',
        'OpenVG_API_identity':'inferred from standard constants/prototypes and native VGU calls',
        'OpenVG_reference':'https://raw.githubusercontent.com/KhronosGroup/OpenVG-Registry/main/api/1.1/openvg.h',
        'functions':{name:window(address,32) for name,address in FUNCTIONS.items()},
        'callers':{name:window(address,size) for name,(address,size) in CALLER_RANGES.items()}}


