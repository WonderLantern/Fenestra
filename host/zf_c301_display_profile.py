"""Verify Zf C3.01 native display entry windows and emit build definitions.

Zf members are paired with their Z9 counterparts and guarded by original Zf
window digests and call-site checks. All instruction data is read from the
locally supplied original EG.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from zf_c301_ui_dispatch import _payload

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'emulation'))
from native_ui_dispatch_sites import verify_original  # noqa: E402

Z9_PROFILE_SOURCE = 'src/platform/z9_c532/display_profile.c'
PINNED_WINDOW_BYTES = 64

# field -> (z9, zf, z9_extent_bytes, sha256 of 64 Zf bytes)
# Unique 24-instruction normalized match; every one is 0 shape mismatches with
# equal Z9/Zf extent.
UNIQUE = {
    'native_menu_progress':            (0x403eb078, 0x403f9d34, 44,   '38688535e1ef52a9e4bdae071e6a0066728d9f53b80e86c7410101d8ed43c7b6'),
    'compound_draw':                   (0x40323d7c, 0x4032e6ec, 3348, '01540cd76e0fcea1680c4b987c0c6c5b0f00cabd2ea10a0c7d3518fec5426afe'),
    'gui_base_initialize':             (0x4031e7b8, 0x40329128, 52,   '86bb79041f56500658622ba0c229957ba908a15e85f45a1886fe88734e34ea1b'),
    'gui_bind_target':                 (0x4031e7f4, 0x40329164, 48,   '9048d5cfa02ca1d179d29c2749e0b260074b2478dc8fa4c0d32a51edcc9592b7'),
    'gui_draw_body':                   (0x40325ecc, 0x4033083c, 176,  'be2fbe136c6758240c349a6a5f3e49ebc108ab70a86186e90feac4c354367909'),
    'gui_extended_draw_body':          (0x40329cc8, 0x40334634, 264,  '88ad8fac0d790e87d744d136d23bbd791c45c4f4d6e865684f7ab6a5420a4fc8'),
    'gui_get_visible':                 (0x403ec99c, 0x403fb660, 20,   '8edc2be1d2e3f079f2d5125459e5c88f6c9a1a23ee858d61cc1ae3575bc44b48'),
    'gui_mark_object_dirty':           (0x403ec980, 0x403fb644, 16,   '4d848bd32b66531e0ac1a9ad3b6f4c7ce36006a5550e006a689e8aad66c7b3c5'),
    'gui_object_insert':               (0x403ebd54, 0x403faa18, 192,  'ccd75b62062c5a97dc88232fbc5ad147efc21c633c9bdb1cf4cbfefb06c4f23b'),
    'gui_object_remove':               (0x403ebd5c, 0x403faa20, 184,  '89faed6fb7b1c48262b10046702cbf30c000aba8f5dc9256fa0c0a728bc827cc'),
    'gui_set_visible':                 (0x403ec990, 0x403fb654, 32,   'a5f66b6c0fcbf832481dadffa863eaa399f2ebbe59153997e65b381a766eec4f'),
    'gui_target_dimensions':           (0x4031e824, 0x40329194, 40,   '28c735461e58fd849ebdc9311988b0a01b2219dd1e901a300c36622f3f973b4a'),
    'native_constant_paint_rectangle': (0x4032a15c, 0x40334ac8, 192,  'd2a112df158ae028370fd9ee788031d8eba2483f62296b6b404b111ce2c20c58'),
    'native_current_page':             (0x403d6d38, 0x403e49d4, 44,   '760d5fa56a7c5effca2662f550652cec8f43665619c892de851a9556625b8ca9'),
    'native_global_surface':           (0x4039a554, 0x403a129c, 28,   'f1defec7d7b60d064b3714b2e751962b88138638b5eefab76401c03e54625a83'),
    'native_global_target':            (0x403ebfb8, 0x403fac7c, 52,   '05ecf6e5958f957ea6a4784cdd9658233a1031f63a63f1a551d441684d2c20dd'),
    'gui_surface_initialize':          (0x40329c30, 0x4033459c, 152,  '37aa0419f5893d6181be59a94c5af39423974259cc38cdb4a040d007bd3125b3'),
    'native_graphics_current_device':  (0x403ecd18, 0x403fba70, 24,   '8ffb2bdd3188665fb60c0fa6d0a46b39eb9741fdb44c0cfa704e4e4077ac9c9c'),
    'native_graphics_target_enabled':  (0x40340e20, 0x4034b994, 52,   '3cb24f52c87debc810afd166c8cdfe85f74bd10c7b25753c4c3598e6a8e7b273'),
    'native_graphics_target_enable':   (0x40340df0, 0x4034b964, 48,   '8204ec561f2d33ff1f39fb8d44b62ef19d8d9fee70aa304d59375229bd71ca1d'),
    'native_graphics_acquire':         (0x4031e328, 0x40328c98, 116,  'cbcd21a79a39a0f159138035aec4bcfc15f0a22dcc7389b7546fa5525c0d3891'),
    'native_graphics_release':         (0x4031e32c, 0x40328c9c, 112,  '27f694a2b5a6abd2fcd498626808272bbbbec172929de0e592cdddcc88c0da3a'),
    'native_list_begin':               (0x40c7b374, 0x40c7b4c0, 8,    'e84e0828be172fe68e96f017f5bdbba24e81d58e334d2b765e727d0462420394'),
    'native_list_count':               (0x40c7afc4, 0x40c7b110, 8,    '8a3a52140c61d678539b01b6b2c96121a6d339aa2452ee5ab499c42d351aee75'),
    'native_list_next':                (0x40c7b130, 0x40c7b27c, 100,  '6e3c44d6ff170a83996dc06d8fdb68426e791109ba4fc1ff82e7f9e6336fedcf'),
    'native_menu_find_row_by_id':      (0x403ebd58, 0x403faa1c, 188,  'ac4d6c08064062a127d91968466ca7a379eca290f8c37ba2d6045a0158ae16d4'),
    'native_mutex_acquire':            (0x40d30004, 0x40d31444, 60,   '355fdaf38ead0ca9adfa421385722457b3730dfbe20d17d45389d8e11e8c6ef3'),
    'native_mutex_release':            (0x40d2ffb0, 0x40d313f0, 72,   '3ae09ebedd5c96cf484696b84ee6ee67149955de66b057d9ca6b16d0ed5fe575'),
    'native_object_dispatch':          (0x403266ec, 0x4033105c, 120,  '4df3d9604704ff19cb14c1d821a009f425fe38253e676fcbb301d03b3b332d3d'),
    'native_rgba_float_conversion':    (0x402e7258, 0x402f1b2c, 344,  '516dedbfe1c3c881790256f990217e540a83120dd8ec0fc169c352176e3d0fd3'),
    'native_transparent_clear_rectangle': (0x4032aa38, 0x403353a4, 164, 'c9a98682e4576827773a0589eb71501b8ecb9ea2eabfb9162985e7c00fc6482e'),
    'native_view_id':                  (0x40394448, 0x4039ae04, 8,    '7168d7370ebfda70815042f8cc62cc865375e78ffd6862f8218b798268111146'),
    'native_view_state':               (0x40394404, 0x4039adc0, 8,    '0391be596a0430a27348eef4150f8a36a56442b00f9033771a4978f05b96b969'),
    'row_draw':                        (0x4031f4ac, 0x40329e1c, 1212, 'da2be6c948c294b7c164b304dad4413fd6d481fae92d6fb92cf94442600fec17'),
    'second_compound_draw':            (0x4032934c, 0x40333cb8, 2276, 'e296e7b84d269f88ddb710dd63c5ec3151276ba0011f8745bffe020f7149b785'),
    'vgAppendPathData':                (0x4025321c, 0x4025321c, 176,  'b895f4301b9a460557f89063618fc61560430a94fb235b2f83d4e090ad88e3a7'),
    'vgClear':                         (0x40252e8c, 0x40252e8c, 104,  '6ee616fd090235447120c90e3834617ec195fb2341409b565d8312225076b83c'),
    'vgCreatePaint':                   (0x40253998, 0x40253998, 92,   '76f8e4c09122a6714309f2ec5d87d452e81a9c97263f41fe247124a051688025'),
    'vgCreatePath':                    (0x40252f0c, 0x40252f0c, 164,  '65ed19a7da9f15d0a369f7ee74f3fe23515e6154b531ee81766ecd0e88d9d4b1'),
    'vgDrawPath':                      (0x402538fc, 0x402538fc, 156,  '9d93856b76caa6bb9dfc82719887b184dd1c391bb8b16a084a3f64ea1e756fcf'),
    'vgGetError':                      (0x402517fc, 0x402517fc, 36,   '0c73d821c97d6a20d0cb40687466252428a723a0798271b2739b1040cb039592'),
    'vgGetPaint':                      (0x40253b2c, 0x40253b2c, 56,   '1af4cbbf1827f184878c9c5eed9b2d115d5575956d2261763f5b4f9a8552e850'),
    'vgSetPaint':                      (0x40253a74, 0x40253a74, 184,  'fcfd4733280f35ef91b9bc127eefde1f4833a06476ae484880600181da4755da'),
    'vgSetParameteri':                 (0x40251db8, 0x40251db8, 380,  '637030df29ecc317bd3e9551ed2aad017bddb81a60cf0659a862a2e8abdc4b17'),
    'vgCreateImage':                   (0x40253d10, 0x40253d10, 288,  '0570944754e1f13181b087c780287cdcb3d29c11e1547dd8de4612fb0fb2fbf4'),
    'vgDrawImage':                     (0x4025449c, 0x4025449c, 152,  '9d75719b99c7d380139aa1f5d11604cee0a01004969fb7992a1adc394eede175'),
    'vgLoadMatrix':                    (0x40252a68, 0x40252a68, 68,   '3ce3bf56ed2ec8ad3b37fecabab8ae78e5c3a13239f8d3e52e7d5487f06aae27'),
    'vgGetMatrix':                     (0x40252ac0, 0x40252ac0, 288,  '0f9972a6ac8fa33a255ea02737aae8aad25e3348054b9dbf93648ece6de52bd3'),
    'vgGetPixels':                     (0x40254730, 0x40254730, 164,  'c60dd9d073835d2b75e4338aa5a183942ec9b8abbe5c7a1a2151c0b135f5e0ba'),
    'vgSetPixels':                     (0x40254534, 0x40254534, 156,  '7bce59dc6272c54822acd7e65ec2d1920169d9e16405a66ff7ebbb5a285b9b06'),
}

# The OpenVG library is byte-for-byte the same *code shape* at the same
# addresses: every normalized word of [0x40250000,0x40258000) is equal between
# the two images (0 of 8192 differing). These ten entries have several
# shape-identical twin entry points inside that one library, so the shape query
# cannot pick among them; the address is retained because the region
# correspondence is established, and the twins execute identical code.
LIVE_VG_REGION = (0x40250000, 0x8000)
LIVE_REGION = {
    'vgClearPath':      (0x40252fb0, 108, '6b47153a4776d08c04eaa4deb46b678127136617c6fc7bd5a3be16c4ab4c5ce0'),
    'vgDestroyPaint':   (0x40253a04, 112, '80c6e3d14eb1080e8099ab5d6d37f1b59f77f38f2d2cddfd25923376366f673d'),
    'vgDestroyPath':    (0x4025301c, 112, '3eec5e0826cf59cc8ef4b1f7ceada61b8c69c0485132345140ee683396aa4f71'),
    'vgGetfv':          (0x40251ae8, 124, '5ca428524c30ed51c7acdeeae8519d2310dd6449199a5ce009874fadde815306'),
    'vgGeti':           (0x40251a84, 100, 'f8a2bd4a680fee7fdad8b04b2fcfb619c2a1f1dd6323873dc3c09f4911b6c30b'),
    'vgSetParameterfv': (0x40251f34, 428, 'a919debc96757d66c45d46cfbe91174298e651f65130e7c59ba85fa93bc1ae07'),
    'vgSetfv':          (0x40251908, 140, '171b618a307532cff04a469d88582ff02d664f2b783f5cf94c7583d5c854ef91'),
    'vgSeti':           (0x402518a8, 96,  '786ce32b45b9af5a6d8fc2e56a32c90793973314980be7d9c13265e0651f863e'),
    'vgDestroyImage':   (0x40253e40, 112, '0465dad0e1f1d7a9dbede97561ae72060d4618dabb571bcd05ad29e1fe9955dd'),
    'vgImageSubData':   (0x40253f74, 176, 'd1822106a64209e1ba54840640aa9b009b3a1d27b257ad15c86469a93c7c03e9'),
}

# Z9 0x403eb0dc has no exact 24-instruction hit; its own-Zf counterpart
# 0x403f9d98 differs in exactly two register moves (+0x1c w19<-w2, +0x20
# w20<-w1; Z9 has the reverse) and in moved global immediates.
COMPOSITOR_SUBMIT = (0x403eb0dc, 0x403f9d98, 1388, 64,
                     '7a2a308129a737c14e3f4c2c3653a4b20953b61122c990ad80ba0a7855a2ab70',
                     'eecb970c7d5589ee4e320d9832b2b0b7784d0b9ad2a9dd1b96db2b8e6e5e4ec4')
COMPOSITOR_SWAP_WORDS = {0x1c: 0x2A0203F3, 0x20: 0x2A0103F4}

# gui_mutex is a data object, not code, so it has no shape. Both GUI wrappers
# build their mutex object in x16 with the same four-instruction MOVZ/MOVK slot
# (Zf wrapper 0x40400384 +0x1c..+0x28, Z9 wrapper 0x403f1588 +0x1c..+0x28).
# Z9 resolves to 0xa032dcb98, which is the Z9 profile's gui_mutex, so the Zf
# slot is the Zf gui_mutex.
GUI_MUTEX = (0xA032DCB98, 0xA03408B68)
GUI_MUTEX_WRAPPERS = {
    'z9': (0x403F1588, {0x1C: 0xD2997316, 0x20: 0xF2A065B6, 0x24: 0xF2C00156, 0x28: 0xF2E00016}),
    'zf': (0x40400384, {0x1C: 0xD2916D16, 0x20: 0xF2A06816, 0x24: 0xF2C00156, 0x28: 0xF2E00016}),
}

# Own-Zf layout values; archive page tables bound the pointer aperture and
# row_draw's original operand binds the child-list offset.
LAYOUT = {
    # Own archived lower root a04d88000[40] points to a04d8f000:
    # identity RAM blocks span a00000000..a40000000; next L1 entry is zero.
    # This is a pointer aperture check, not a promise that every page is mapped.
    'ram_begin': 0xA00000000,
    'ram_end': 0xA40000000,
    # Own row_draw adds x20+0x128 before list begin/count/next.
    'row_children_offset': 0x128,
}
UNPINNED = {




}


# Identified from corresponding original callers, not nearest code shape.
# Surface #4 and its global target are paired at 403dc880/403dc898.
# The APPLV context a02832f70 has id 3 in the retained own-device record.
RESOLVED = {
    'current_view_pointer': 0xa033749d0,
    'native_graphics_backing_rectangle': 0x4034bd24,
    'global_surface_index': 4,
    'shooting_view_id': 3,
}
RESOLVED_PINS = [(1077198116, 116, '985d862ca5e0e64bccce3a97e858b37eb0aaa3617bffa8794d72f0c79f32476c'), (1077467236, 24, 'a9fd438b405232b54979cf051b34a9b7d0d1d463ee0ca8a0725bb75ce8068d1c'), (1077467276, 24, 'de9a318039f663d2e126833b54c838373ee49466f15c323c49c6a4c884cf7417'), (1077472392, 24, '442856343fc1386c7018f618b307bcdf5d19da17d1227c891dfb7ad2e8d24c9f'), (1077472772, 24, 'f1634c3ddda471a873a5f5c074d73362733efcc82a926c192b87bf18ceda2923'), (1077790848, 12, 'd2be59de066e29bcc151727de847bcf188cde41be1a4d502f16e9844335081dc'), (1077790872, 12, '33daf974396c9aca410bca8e5e5a30d52cfe590621985a199a9b82fe7cb56fa8'), (1078004012, 20, 'c402bce27686015a951609818e3862293a4e3f7b5e41cc683f0c68909ea63e75'), (1078004032, 44, 'cc2872399d0dcccc79302d5aa32297704209f7b3fe0fa265adb625aa2b58e2c4')]

def verify_display_profile(payload):
    z9_source = (ROOT / Z9_PROFILE_SOURCE).read_text(encoding='ascii')
    z9_fields = {}
    for part in z9_source.split('.')[1:]:
        if '=FN(' not in part:
            continue
        name, value = part.split('=FN(', 1)
        z9_fields[name.split()[0].strip()] = int(value.split(')')[0], 16)
    # 69 Z9 members = 50 unique + 10 live-region + native_menu_compositor_submit
    # + gui_mutex + 3 layout members + the 4 unpinned members.
    if len(z9_fields) != len(UNIQUE) + len(LIVE_REGION) + 2 + len(LAYOUT) + len(UNPINNED) + len(RESOLVED):
        raise ValueError('Z9 display profile member count changed')
    for name, (z9, address, _extent, digest) in UNIQUE.items():
        if z9_fields.get(name) != z9:
            raise ValueError('Z9 display member changed: ' + name)
        original = payload[address - 0x40160000:address - 0x40160000 + PINNED_WINDOW_BYTES]
        if hashlib.sha256(original).hexdigest() != digest:
            raise ValueError('Zf display window differs: ' + name)
    for name, (address, _extent, digest) in LIVE_REGION.items():
        if z9_fields.get(name) != address:
            raise ValueError('Z9 display member changed: ' + name)
        original = payload[address - 0x40160000:address - 0x40160000 + PINNED_WINDOW_BYTES]
        if hashlib.sha256(original).hexdigest() != digest:
            raise ValueError('Zf display window differs: ' + name)
    z9_submit, zf_submit, extent, length, digest, z9_digest = COMPOSITOR_SUBMIT
    if z9_fields.get('native_menu_compositor_submit') != z9_submit:
        raise ValueError('Z9 compositor submit member changed')
    original = payload[zf_submit - 0x40160000:zf_submit - 0x40160000 + length]
    if hashlib.sha256(original).hexdigest() != digest:
        raise ValueError('Zf compositor submit window differs')
    for offset, word in COMPOSITOR_SWAP_WORDS.items():
        if int.from_bytes(payload[zf_submit - 0x40160000 + offset:][:4], 'little') != word:
            raise ValueError('Zf compositor submit register move differs')
    if int.from_bytes(payload[0x40329ec4-0x40160000:0x40329ec8-0x40160000], 'little') != 0x9104A295:
        raise ValueError('Zf row children list operand differs')
    zf_base, zf_words = GUI_MUTEX_WRAPPERS['zf']
    for offset, word in zf_words.items():
        if int.from_bytes(payload[zf_base - 0x40160000 + offset:][:4], 'little') != word:
            raise ValueError('Zf gui mutex slot differs')
    for address,size,digest in RESOLVED_PINS:
        at=address-0xa02808000+0x4777000 if address>=0xa02808000 else address-0x40160000
        if hashlib.sha256(payload[at:at+size]).hexdigest()!=digest:
            raise ValueError('Zf resolved display pin differs: '+hex(address))
    return dict(z9_members=len(z9_fields), unique=len(UNIQUE), live_region=len(LIVE_REGION),
                compositor_submit=1, gui_mutex=1, unpinned=sorted(UNPINNED),
                z9_compositor_sha256=z9_digest, pinned_window_bytes=PINNED_WINDOW_BYTES,
                live_region_range=[hex(LIVE_VG_REGION[0]), LIVE_VG_REGION[1]])


def prepare(application, output):
    output = Path(output).resolve()
    if not output.is_relative_to(ROOT / 'build') or output == ROOT / 'build' or output.exists():
        raise ValueError('choose a new output directory below build/')
    payload = _payload(application)
    checks = verify_original('zf-c301', payload)
    verified = verify_display_profile(payload)
    values = {}
    for name, (z9, address, _extent, _digest) in UNIQUE.items():
        values[name] = address
    for name, (address, _extent, _digest) in LIVE_REGION.items():
        values[name] = address
    values['native_menu_compositor_submit'] = COMPOSITOR_SUBMIT[1]
    values['gui_mutex'] = GUI_MUTEX[1]
    values.update(LAYOUT)
    values.update(RESOLVED)
    output.mkdir(parents=True)
    header = ['/* Zf C3.01 original GUI/OpenVG windows, hash-checked; experimental, not runtime qualified. */',
              '#ifndef FX_ZF_DISPLAY_PROFILE_H', '#define FX_ZF_DISPLAY_PROFILE_H']
    header += [f'#define FX_ZF_DISPLAY_{name.upper()} UINT64_C(0x{values[name]:x})' for name in sorted(values)]
    header += [f'#define FX_ZF_DISPLAY_PINNED_FIELDS {len(values)}u',
               f'#define FX_ZF_DISPLAY_UNPINNED_FIELDS {len(UNPINNED)}u',
               '#endif', '']
    (output / 'fx_zf_display_profile.h').write_text('\n'.join(header), encoding='ascii')
    result = dict(schema='fenestra.zf-c301-display-profile/v1',
                  original_edges=checks, verified=verified,
                  z9_profile_source=Z9_PROFILE_SOURCE,
                  pinned={name: dict(z9=hex(z9), address=hex(address), extent_bytes=extent, sha256=digest)
                          for name, (z9, address, extent, digest) in UNIQUE.items()},
                  live_region={name: dict(address=hex(address), extent_bytes=extent, sha256=digest)
                               for name, (address, extent, digest) in LIVE_REGION.items()},
                  compositor_submit=dict(z9=hex(COMPOSITOR_SUBMIT[0]), address=hex(COMPOSITOR_SUBMIT[1]),
                                         extent_bytes=COMPOSITOR_SUBMIT[2],
                                         sha256=COMPOSITOR_SUBMIT[4], z9_sha256=COMPOSITOR_SUBMIT[5],
                                         swap_words={hex(k): hex(v) for k, v in COMPOSITOR_SWAP_WORDS.items()}),
                  gui_mutex=dict(z9=hex(GUI_MUTEX[0]), address=hex(GUI_MUTEX[1]),
                                 method='same MOVZ/MOVK slot in both GUI wrappers'),
                  resolved={name:hex(value) for name,value in RESOLVED.items()},
                  layout={name:hex(value) for name,value in LAYOUT.items()},
                  unpinned={name: reason for name, reason in sorted(UNPINNED.items())},
                  pinned_field_count=len(values), unpinned_field_count=len(UNPINNED),
                  shape_query_only=True, member_roles_inherited_from_z9_order=True,
                  runtime_qualified=False, firmware_output_generated=False)
    (output / 'display-profile.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', required=True)
    parser.add_argument('--output-dir', required=True)
    arguments = parser.parse_args()
    print(json.dumps(prepare(arguments.application, arguments.output_dir), indent=2))
