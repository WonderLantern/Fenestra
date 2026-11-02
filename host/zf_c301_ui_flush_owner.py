"""Verify the Zf C3.01 UI flush profile and emit local build definitions.

These Zf entry points and child IDs were derived by comparing native Z9 and
Zf implementations. Builds verify the pinned Zf image directly; a Z9 firmware
copy and the private research workspace are not required.
"""
import hashlib
import json
from pathlib import Path
from zf_c301_ui_dispatch import _payload

BASE = 0x40160000
# macro -> (Zf address, SHA-256 of its 64-byte entry window)
PINS = {
    'FX_ZF_FLUSH_COUNT_ROWS': (0x403b894c, '8e82b2d1aab8e16b3e42cf8f435242010c6af59fa3da6a893b699e1ebc8bc7d4'),
    'FX_ZF_FLUSH_HIGHLIGHT': (0x403b8a60, '27df7055be3f9c7534eb9686401db368138ccb499c49663163c77f9ac7fdfb55'),
    'FX_ZF_FLUSH_RESET_CHECK': (0x403b8ba0, '3fedda2fbc44ed63644fa8972e7eea88fee422e4f8c44815c5267c0480fdac1b'),
    'FX_ZF_FLUSH_REPAIR_SELECTION': (0x403b89c8, 'a90fce833da14f7e5d44266c6ab796c223ed67bb7840bb3c2e6d4168ce5bc54b'),
    'FX_ZF_FLUSH_PLACE_ROWS': (0x403b735c, '6ac87ecbf771ae1ed8dcc05d4de5ebee5524e6db6ccb792bd099037758a2e50f'),
    'FX_ZF_FLUSH_RESIZE_CANVAS': (0x403b7620, '689928146d0c23a3dc538c52fc12e73d663a0d96c57ebfa922a7ba8767c7de5d'),
    'FX_ZF_FLUSH_SCROLLBAR': (0x403b8e44, '7515334c83a9cbb14c1b7c6beb174e0317706cd0a924d13bcbc2bd8aeaff17ba'),
    'FX_ZF_FLUSH_PREPARE_VIEWPORT': (0x403b7874, 'bfe54c266870956d555b167a776143099a864d02a70e9942a2583af1b780092b'),
    'FX_ZF_ROW_LOOKUP': (0x4033091c, '90f6e8fb069521d1c62ee3a2554a17f0f7d29c149dd6a1d7aa4f3b2f6216485f'),
    'FX_ZF_SET_ACTIVE': (0x403fb694, '2c4ccfe3cf60f4ce2b818290aaf93e370c45d30891cfda047bada8894d8a478e'),
    'FX_ZF_ROW_DRAW': (0x40329e1c, 'da2be6c948c294b7c164b304dad4413fd6d481fae92d6fb92cf94442600fec17'),
    'FX_ZF_CHILD_LOOKUP': (0x4032a3cc, '0e0ff01ebc0ce36809896bb22e5cb06c48f350c02cf0d0a4164cc4a7ce2404ca'),
    'FX_ZF_SET_DISABLED': (0x403fb674, 'd8414494120fea9338cc789d52ea8e95c73e39a93504db733b535cc584f5df0d'),
    'FX_ZF_SET_TEXT': (0x403fb6b4, '5e749c10f5fd25d4c7aaf8da2e0e35ed727ee83f54c9ea74882c9403faa9f3ff'),
    'FX_ZF_RELEASE_TEXT': (0x403367a4, 'c15174cc2f0ce5b99918a2704688f3ca03031a1d53115b11a407f647add3155c'),
    'FX_ZF_SET_ALIGNMENT': (0x403fb888, 'b8d62beeeee082337379665fc150940efb695bd6fa4397bc53a3f1b90a98024d'),
    'FX_ZF_MEASURE': (0x403291ec, 'f4cf098d03367e3deffb4042917c5bbb89baab559d123bd039d0d6f815db7077'),
    'FX_ZF_BOUNDS': (0x403290c8, '22ea224b402f370f4f0bf140200cae427434a7b7af36b24bb4735ff1be004928'),
    'FX_ZF_TITLE_MARK_DIRTY': (0x403fb644, '4d848bd32b66531e0ac1a9ad3b6f4c7ce36006a5550e006a689e8aad66c7b3c5'),
    'FX_ZF_TITLE_DESTRUCTOR': (0x40331924, '66224712e478b71143382853559676e875b42d3fdc76e99f0d1e422ae7131eb1'),
    'FX_ZF_TITLE_MODAL_DESTRUCTOR': (0x4032f560, '7e4e5726b5945858561595ddd008bd8df93b30bce0bb90161b1b2c59614e3694'),
    'FX_ZF_COMMIT_SUBMIT': (0x403f9d98, '7a2a308129a737c14e3f4c2c3653a4b20953b61122c990ad80ba0a7855a2ab70'),
    'FX_ZF_COMMIT_OBJECT': (0x403f9d34, '38688535e1ef52a9e4bdae071e6a0066728d9f53b80e86c7410101d8ed43c7b6'),
}

# macro -> (Zf value, native immediate sites carrying that value)
IDS = {
    'FX_ZF_VALUE_CHILD': (0x4917, (1077609208, 1077616324, 1077641224, 1077644048, 1077808772, 1078845040)),
    'FX_ZF_ARROW_CHILD': (0x4918, (1077555124, 1077555732, 1077610280, 1077616984, 1079719008, 1079746560)),
    'FX_ZF_MODAL_ID': (0x491d, (1077642624, 1077659728, 1077790132, 1077791032, 1077792836, 1077795672)),
    'FX_ZF_ROW_BYTES': (0x140, (1076367272, 1076367488, 1076674644, 1076675192, 1076888712, 1077012532)),
    'FX_ZF_VALUE_BYTES': (0x118, (1075257584, 1075858376, 1075862348, 1075865464, 1075868056, 1075891200)),
}


def verify(application):
    payload = _payload(application)
    pins = []
    for macro, (address, digest) in PINS.items():
        at = address - BASE
        if hashlib.sha256(payload[at:at + 64]).hexdigest() != digest:
            raise ValueError('Zf flush entry differs: ' + macro)
        pins.append(dict(macro=macro, zf=hex(address), ok=True))
    ids = []
    for macro, (value, sites) in IDS.items():
        for address in sites:
            word = int.from_bytes(payload[address - BASE:address - BASE + 4], 'little')
            immediate = ((word >> 5) & 0xffff) << (16 * ((word >> 21) & 3))
            if (word & 0x7f800000) not in (0x52800000, 0x52a00000, 0x72800000, 0x72a00000, 0x12800000) or immediate != value:
                raise ValueError('Zf flush child/layout value differs: ' + macro)
        ids.append(dict(macro=macro, expected=hex(value), agrees=True))
    # Native calls establish submit(callback, w1=3, w2=7).
    expected = (0xd293a680, 0xf2a807e0, 0xf2c00000, 0xf2e00000, 0x320007e1, 0x32000be2)
    for site in (0x403a7458, 0x403b9fa4):
        at = site - BASE
        words = tuple(int.from_bytes(payload[i:i+4], 'little') for i in range(at-24, at, 4))
        branch = int.from_bytes(payload[at:at+4], 'little')
        delta = branch & 0x3ffffff
        if delta & 0x2000000:
            delta -= 0x4000000
        if words != expected or branch >> 26 != 0x25 or site + delta * 4 != 0x403f9d98:
            raise ValueError('Zf compositor submit convention differs')
    return dict(pins=pins, ids=ids, all_pins_ok=True, all_ids_ok=True,
                commit_argument_convention_established=True)


def prepare(application, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    receipt = verify(application)
    lines = ['/* Zf C3.01 native UI flush profile; verified against the local original. */',
             '#ifndef FX_ZF_FLUSH_PROFILE_H', '#define FX_ZF_FLUSH_PROFILE_H']
    lines += [f"#define {r['macro']} UINT64_C({r['zf']})" for r in receipt['pins']]
    lines += [f"#define {r['macro']} {r['expected']}u" for r in receipt['ids']]
    lines += [f'#define FX_ZF_FLUSH_PINNED_FUNCTIONS {len(PINS)}u',
              f'#define FX_ZF_FLUSH_PINNED_IDS {len(IDS)}u', '#endif']
    header = output / 'fx_zf_flush_profile.h'
    header.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    receipt['header'] = dict(path=header.name, sha256=hashlib.sha256(header.read_bytes()).hexdigest())
    (output / 'flush-owner.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt
