"""Zf keyboard/dialog records pinned to C3.01 original data and call sites."""
import hashlib
import json
from pathlib import Path
from zf_c301_ui_dispatch import _payload

PINS = {'keyboard_page': (42992705464, 96, '09577ce5bda3db6efb1e8322246eea621b0683731a01283a0b8abbcc3fb6e49d'), 'keyboard_descriptor': (42992459904, 96, 'd35a63575e1fad1edf49393e9b20e84a2711fba624a44d79975c9649d4624bbc'), 'dialog_descriptor': (42992377424, 64, '6deffe16700c5b77ebf9667c6a3f567b90c22e6ccf3e27f9f1a47b1c58d0c13c'), 'dialog_rows': (1132882076, 88, 'd21f6526db22775fcc293fb320bba1239c76a623e6081e45c5c2ca6f94233ea1'), 'keyboard_initialize': (1080149384, 128, '071939bc7b8f5726dc646a73ae605c643ee43a2cf40092cf842b00db092612a0'), 'keyboard_read': (1080157828, 124, '42359a545f20d8ce1ea6f5cccbab079183a8bc8ca8431adc8d0b65b665b7b679'), 'keyboard_ready': (1079103136, 176, 'b25eb61145b6df8731f9307d7c20332936491e20dcb4df77e5bcb3b7dc0b1afe'), 'keyboard_transition': (1077654080, 120, '179b58d38c9fc9aa117a916956d3ed7271588768b24cd8d07fb9b667dea91778'), 'keyboard_condition': (1077652372, 64, '5e8f912c4ba599a7850e6ea2e343362934f2fed4e15e9d98cf5d0bd0fae0c4f3'), 'keyboard_opener': (1079678240, 112, '8cb832aa65d52aecb633be6515a5c3c7ca7b9cd179e16d317d98a14c92b82eca'), 'keyboard_events': (1136114632, 512, 'cb6a86b51b61b7757da0c1cf1595c8c740019388b66e720fce196247c0cedb2e')}
VALUES = {'keyboard_page': 42992705464, 'dialog_descriptor': 42992377424, 'dialog_rows': 1132882076, 'keyboard_initialize': 1080149384, 'keyboard_read': 1080157828, 'keyboard_ready': 1079103136, 'keyboard_transition': 1077654080, 'current_page': 1077823956}

def verify(payload):
    for name, (address, size, digest) in PINS.items():
        at=address-0xa02808000+0x4777000 if address>=0xa02808000 else address-0x40160000
        if hashlib.sha256(payload[at:at+size]).hexdigest()!=digest:
            raise ValueError('Zf special UI bytes differ: '+name)
    return dict(pins=len(PINS),values={k:hex(v) for k,v in VALUES.items()})

def prepare(application, output):
    output=Path(output)
    if output.exists():raise ValueError('special UI output exists')
    receipt=verify(_payload(application))
    output.mkdir(parents=True)
    lines=['#ifndef FX_ZF_SPECIAL_UI_PROFILE_H','#define FX_ZF_SPECIAL_UI_PROFILE_H']
    lines.extend('#define FX_ZF_UI_'+k.upper()+' UINT64_C('+hex(v)+')' for k,v in VALUES.items())
    lines+=['#endif','']
    (output/'fx_zf_special_ui_profile.h').write_text('\n'.join(lines),encoding='ascii')
    (output/'special-ui-profile.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt
