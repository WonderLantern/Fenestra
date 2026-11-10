"""Hash-check Zf native timer/UI edges for an experimental dispatch profile.

This establishes original call sites and helper ABIs, not a lifecycle owner,
an installed hook, or GUI qualification. No firmware bytes are modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import struct

from zf_c301_ui_dispatch import _payload

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'emulation'))
from native_ui_dispatch_sites import PROFILES, verify_original, sites

PREIMAGES = {
    'current_page': (0x403e49d4, 44, '7fc9d8a49446980f43cdb5c971ddfe55a5b8e836c381562aefc9b8fab8e1f23f'),
    'poll': (0x403dfec8, 304, '55d4b2840340c908c6cdde43367cb8e7af2669a93d3be32d4fd26ef11f5787da'),
    'wait': (0x403dfff8, 136, 'b464cb3acac9f7b9e46e51e6852a0dfff4b62a756622286320dfe6071fa99150'),
    'event_group': (0x403e45a4, 20, 'ac8195fdbb46151048594ae4700c128a6e6b8ed3719a2d99fe7604c5cfdaf180'),
    'event_set': (0x403e4670, 16, '31ef77c73f10782138eabd12f946d8edf8aea7927bab4cb4b6e4fcf0de8c8ef9'),
}

GUI_WINDOWS = {
    'gui_wrapper': (0x40400384, 100, 'b99e51cdb21fe93651ea02a55b1027e4b587d3e96701ae35744091d7cc660c83'),
    'gui_enter': (0x40d31444, 60, 'c58c877e888106c9ce3defbd7d110cfbf1465762668ffae60adbe1cd28827803'),
    'gui_leave': (0x40d313f0, 84, 'ee817e75cdab772998a46a08d16a2323d5ad8c6605c90b7a80bda4edb4a4480b'),
    'navigate': (0x403b8024, 340, '6619380fc60ddc80304e988574a6df7284b91ddaaa100d3b8f7646b584250e14'),
}

RENDER_WINDOWS = {
    'scrollbar': (0x403b8e44,168,'602acf1c2dbd1cccf41d9a68e888ca13a1b0445e78066e1f55482f60e1ba3dc1'),
    'value_child_route': (0x403b8b10,16,'c96b6664c8d5ff997199eb4d42a4b2ffb4891db2d9812abd56475dde8a8195d2'),
    'display_notify_prefix': (0x403f9d98,96,'ea8000f7a985d129e043484bc105fad053955b9b8684f4b98e3d386dbbe6c061'),
}


# Zf windows paired with the six Z9 FxNativeMenuDrawProfile components.
# Verify the Zf entry windows directly before generating their definitions.
DRAW_PROFILE = {
    'dirty':     (0x403f196c, 0x40400768, 64,  '6d0d5f20ba03ffd3ebacc945fc6a0cb1b913b9395ec4518f25df5aeb47d765a7'),
    'dispatch':  (0x403266ec, 0x4033105c, 120, 'dd6be1153b0d67860dd4e01245f6dd8809d9820fdf83a56d1c08de0f17373b09'),
    'device':    (0x403ebfb8, 0x403fac7c, 64,  '05ecf6e5958f957ea6a4784cdd9658233a1031f63a63f1a551d441684d2c20dd'),
    'rectangle': (0x403efcec, 0x403fea74, 64,  '199e4beb404fd2bb2eef2d644f4757ac4e6640f5c1473168539e4623b0c8ca50'),
    'empty':     (0x40c7cb34, 0x40c7cc80, 64,  '7a1a9dab22bb4e0f9959b7ca5827479e421d3b3b5512195b7751fc5bf586ae6e'),
    'lookup':    (0x40325fac, 0x4033091c, 64,  '90f6e8fb069521d1c62ee3a2554a17f0f7d29c149dd6a1d7aa4f3b2f6216485f'),
}
Z9_DRAW_MODAL_ID = 0x4bf1


def verify_draw_profile(payload):
    for name, (z9, address, length, digest) in DRAW_PROFILE.items():
        if hashlib.sha256(payload[address-0x40160000:address-0x40160000+length]).hexdigest()!=digest:
            raise ValueError('Zf draw component differs: '+name)
    def word(address):return struct.unpack_from('<I',payload,address-0x40160000)[0]
    if DRAW_PROFILE['dispatch'][1]!=0x4033105c:
        raise ValueError('Zf draw dispatch address differs')
    # 0x4033105c loads the page draw callback from [x20+0xb0] and tail-calls it.
    for address, value in ((0x40331070,0xf9405a88),(0x40331080,0xf9405a83),(0x40331098,0xd61f0060)):
        if word(address)!=value:
            raise ValueError('Zf draw callback offset or tail call differs')
    # Display-notify entry keeps the Z9 stack shape but swaps the saved arguments.
    for address, value in ((0x403f9db4,0x2a0203f3),(0x403f9db8,0x2a0103f4)):
        if word(address)!=value:
            raise ValueError('Zf display notify argument order differs')
    return dict(components={name:hex(address) for name,(_,address,_,_) in DRAW_PROFILE.items()},
                draw_callback_offset=0xb0, draw_dispatch_is_gui_edge=True,
                notify_argument_order='w19<-w2,w20<-w1',z9_modal_id=hex(Z9_DRAW_MODAL_ID),
                zf_modal_id=None,draw_profile_bound=True,full_ui_flush_qualified=False)


def verify_render_layout(payload):
    for name,(address,length,digest) in RENDER_WINDOWS.items():
        if hashlib.sha256(payload[address-0x40160000:address-0x40160000+length]).hexdigest()!=digest:
            raise ValueError('Zf render preimage differs: '+name)
    def word(address):return struct.unpack_from('<I',payload,address-0x40160000)[0]
    if word(0x403b8e68)!=0x321f0be0:
        raise ValueError('Zf scrollbar selector differs')
    value=word(0x403b8b10)
    if value&0xffe0001f!=0x52800001 or (value>>5)&65535!=0x4917:
        raise ValueError('Zf value child differs')
    for pc,target in [(0x403b8e6c,0x403a5a00),(0x403b8b18,0x403fb8b4)]:
        value=word(pc);delta=value&0x3ffffff
        if delta&(1<<25):delta-=1<<26
        if value>>26!=0x25 or pc+4*delta!=target:
            raise ValueError('Zf render child lookup differs')
    return dict(scrollbar='0x403b8e44',scrollbar_selector=14,value_child=0x4917,
                display_notify='0x403f9d98',full_ui_flush_qualified=False)


def verify_gui(payload):
    for name, (address, length, digest) in GUI_WINDOWS.items():
        if hashlib.sha256(payload[address-0x40160000:address-0x40160000+length]).hexdigest()!=digest:
            raise ValueError('Zf GUI preimage differs: '+name)
    for address, target in ((0x404003b4,0x40d31444),(0x404003c4,0x4033105c),(0x404003d0,0x40d313f0)):
        word=struct.unpack_from('<I',payload,address-0x40160000)[0]
        immediate=word&0x3ffffff
        if immediate&(1<<25):immediate-=1<<26
        if word>>26!=0x25 or address+immediate*4!=target:
            raise ValueError('Zf GUI call edge differs')
    value=0
    for index in range(4):
        word=struct.unpack_from('<I',payload,0x404003a0-0x40160000+index*4)[0]
        if word&31!=22 or word&0xff800000!=(0xd2800000 if not index else 0xf2800000):
            raise ValueError('Zf GUI exclusion object construction differs')
        shift=((word>>21)&3)*16
        value=(value&~(65535<<shift))|(((word>>5)&65535)<<shift)
    if value!=0xa03408b68:raise ValueError('Zf GUI exclusion object differs')
    return dict(wrapper='0x40400384',enter='0x40d31444',leave='0x40d313f0',
                dispatch='0x4033105c',object=hex(value),navigate='0x403b8024',native_ui_flush_implemented=False)


def prepare(application, output):
    output = Path(output).resolve()
    if not output.is_relative_to(ROOT / 'build') or output == ROOT / 'build' or output.exists():
        raise ValueError('choose a new output directory below build/')
    payload = _payload(application)
    checks = verify_original('zf-c301', payload)
    gui = verify_gui(payload)
    render = verify_render_layout(payload)
    draw = verify_draw_profile(payload)
    profile = PROFILES['zf-c301']
    fields = ('current_page', 'poll', 'wait', 'event_group', 'event_set', 'wake_mask')
    values = dict(profile, current_page=0x403e49d4)
    for key, (address, length, digest) in PREIMAGES.items():
        original = payload[address - 0x40160000:address - 0x40160000 + length]
        if values[key] != address or hashlib.sha256(original).hexdigest() != digest:
            raise ValueError('Zf dispatch helper differs: ' + key)
    output.mkdir(parents=True)
    header = ['/* Zf C3.01 original call edges checked; experimental, not runtime qualified. */',
              '#ifndef FX_ZF_DISPATCH_PROFILE_H', '#define FX_ZF_DISPATCH_PROFILE_H']
    header += [f'#define FX_ZF_DISPATCH_{key.upper()} UINT64_C(0x{values[key]:x})' for key in fields]
    header += [f'#define FX_ZF_GUI_{key.upper()} UINT64_C({value})' for key,value in gui.items() if key!='native_ui_flush_implemented']
    header += [f'#define FX_ZF_DRAW_{name.upper()} UINT64_C(0x{address:x})'
               for name,(_,address,_,_) in DRAW_PROFILE.items()]
    header += ['#define FX_ZF_PAGE_DRAW_CALLBACK_OFFSET UINT64_C(0xb0)', '#endif', '']
    (output / 'fx_zf_dispatch_profile.h').write_text('\n'.join(header), encoding='ascii')
    result = dict(schema='fenestra.zf-c301-dispatch-profile/v1', original_edges=checks,
                  gui_exclusion=gui,
                  render_layout=render,
                  draw_profile=draw,
                  draw_preimages={name: dict(z9=hex(z9),address=hex(a),bytes=n,sha256=h)
                                  for name,(z9,a,n,h) in DRAW_PROFILE.items()},
                  preimages={key: dict(address=hex(a), bytes=n, sha256=h) for key, (a,n,h) in PREIMAGES.items()},
                  helpers={key: hex(values[key]) for key in fields},
                  hook_sites={hex(address): symbol for address, symbol in sites('zf-c301').items()},
                  original_calls_verified=True, runtime_owner_verified=False,
                  runtime_qualified=False, hook_installed=False, firmware_output_generated=False)
    (output / 'dispatch-profile.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.application, args.output_dir), indent=2))
