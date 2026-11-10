"""Own-Zf native SDK entry bodies, checked against retained EG decompilation ranges."""
import hashlib

PINS = {'camera.ui.editor.init': (1080149384, 5636, 'ba2f95bde8fdde8dda8396a855c8a427f674a8020920bdaaeb6f1246c2de23d8'), 'camera.ui.editor.read': (1080157828, 124, '42359a545f20d8ce1ea6f5cccbab079183a8bc8ca8431adc8d0b65b665b7b679'), 'camera.ui.editor.insert-byte': (1080163364, 252, '7d09d0b8e9238c83febd1466242bdc9b6c79d95cdcd841dfd90270fbdb72ac31'), 'camera.ui.editor.delete-byte': (1080158184, 124, 'ddd56760a385d8d0ccd6b5d819d987864adb8983a64a08e8f3ccb3f2c6768261'), 'camera.ui.editor.hit': (1080163784, 256, 'b79bc0b9eafffbf49b7af6c4ae35e8067b9e891372db53e6f770de7da953ed69'), 'camera.ui.text.measure': (1077055980, 152, 'e8f47a69072c0ff2461404ff262e78cf0305bb8909e9f5218b90f71a8b5e44f2'), 'camera.ui.text.align': (1077917832, 28, 'cd51f3005c601d047ee16e8bb0310e9a2f68ce6fca24bde79f2337fba7240ee9'), 'camera.ui.bounds.get': (1077055688, 40, '247af50b71927fe902be4ca9990a958ab161949b5287da8976df5ff312dcdfe3'), 'camera.ui.bounds.set': (1077055588, 100, '185b8d6499850680f68fed8518d6859932bbb153f404b3d747429c13f9a06890'), 'mmu.context.wrapper': (1075243880, 56, '1b23aba3767e0cbf9b340fc989daaa56f392d25af692946f3cf7082a7b133a17'), 'irq.controller.initialize': (1075232720, 312, '7e43497dd79f41d37074f47490b085f3c72cce1bd0be5f54fa16901f73ca81bd'), 'irq.controller.wrapper': (1075235060, 36, 'c5c560710acb72f708ec039e2abf244531097121a7e9d933d62d621acebc1203')}

FUNCTIONS = {name: spec[0] for name,spec in PINS.items() if name.startswith('camera.')}
TABLES = {
    'camera.ui.editor.template': (0xa02909fb8, 96),
    'camera.menu.page-roots': (0xa03401b88, 56),
    'camera.locale.font-root': (0xa0304f6a0, 8),
    # Match the original SDK's readable prefix; this is not the full controller.
    # Own initializer clears +0x20..+0x2300 and writes +0x2330; lock starts +0x2338.
    'rtos.irq.controller': (0xa04d4c0a0, 0x2338),
}

def verify(payload):
    for name,(address,size,digest) in PINS.items():
        at=address-0x40160000
        if hashlib.sha256(payload[at:at+size]).hexdigest()!=digest:
            raise ValueError('Zf native SDK preimage differs: '+name)
    from zf_c301_special_ui_profile import verify as special_verify
    special_verify(payload)
    # Template page is the original keyboard page 0x2c9, group 2.
    at=0xa02909fb8-0xa02808000+0x4777000
    if int.from_bytes(payload[at:at+4],'little')!=(0x2c9<<16|2):
        raise ValueError('Zf keyboard template page identity differs')
    return dict(functions=len(FUNCTIONS),pins=len(PINS),tables=TABLES)
