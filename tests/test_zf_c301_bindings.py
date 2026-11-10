"""Static Zf RTOS binding inventory checks; no camera or native CPU."""
import hashlib
import os
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
import zf_c301_bindings as bindings


APPLICATION = Path(os.environ.get('FENESTRA_ZF_EG', '__no_local_zf_eg__'))


class ZfBindingTests(unittest.TestCase):
    def test_inventory_has_explicit_zf_rebindings(self):
        self.assertEqual(len(bindings.RTOS_FUNCTIONS), 60)
        self.assertEqual(bindings.RTOS_FUNCTIONS['rtos.memory.allocate'], 0x40D31898)
        self.assertEqual(bindings.RTOS_FUNCTIONS['rtos.memory.release'], 0x40D318D8)
        self.assertEqual(bindings.RTOS_TABLES['rtos.mmu.context']['address'], 0xA04D53580)
        self.assertEqual(bindings.RTOS_TABLES['rtos.irq.controller']['address'], 0xA04D4C0A0)
        self.assertEqual(set(bindings.MENU_FUNCTIONS), {
            'camera.menu.page.get', 'camera.menu.text.get', 'camera.menu.navigate'})

    def test_pinned_eg_windows_verify_when_available(self):
        if not APPLICATION.is_file():
            self.skipTest('local recovered Zf EG is unavailable')
        result = bindings.verify_application(APPLICATION)
        self.assertEqual(result['schema'], 'fenestra.zf-c301-bindings/v1')
        self.assertEqual(result['application_bytes'], 79144341)
        self.assertEqual(result['application_sha256'],
                         '6891edd22d9a40042e4718480d29082682bbd12aea172c51f4f164b97f74fe08')
        self.assertFalse(result['runtime_qualified'])
        self.assertEqual(len(result['functions']), 60)
        payload=APPLICATION.read_bytes()[:-2]
        header=bindings._header(result,payload)
        self.assertIn('"camera.display.scene",FX_BIND_DEVICE',header)
        self.assertIn('(uintptr_t)fx_native_scene_resource',header)
        for name in ('core.schedule','core.tick','core.memory','card.files','core.services','core.resources','core.capacity'):
            self.assertIn('"'+name+'",FX_BIND_',header)
        # These original shared builtins already exist in the linked Core;
        # the Zf target must expose the same names and compile guards as Z9.
        for name,symbol in [('core.context','fx_native_context'),
                            ('core.translation','fx_native_translation'),
                            ('rtos.irq.mailbox','fx_native_irq_mailbox'),
                            ('rtos.event.signal','fx_native_event_signal'),
                            ('camera.ui.input.text','fx_native_ui_text_input'),
                            ('core.ui.document','fx_ui_document_resource'),
                            ('core.tasks','fx_native_tasks_resource')]:
            self.assertIn('"'+name+'",FX_BIND_',header)
            self.assertIn('(uintptr_t)'+symbol,header)
        initializer=header.split('static const FxNamedBinding fx_zf_c301_rtos_bindings[] = {',1)[1].split('};',1)[0]
        self.assertNotIn('extern ',initializer)
        self.assertIn('"rtos.mmu.context",FX_BIND_TABLE,FX_CALL_BYTES,1,FX_BIND_READ|FX_BIND_ESCAPE,FX_SDK_BINDING,88u,UINT64_C(0xa04d53580)',initializer)
        for row in result['menu_functions']:
            self.assertIn('"'+row['name']+'",FX_BIND_FUNCTION',initializer)
        from zf_c301_sdk_profile import FUNCTIONS,TABLES,PINS,verify
        from platform_profiles import PROFILES
        original=PROFILES['z9-c532']
        self.assertTrue(set(original['functions']).issubset(
            set(bindings.RTOS_FUNCTIONS)|set(bindings.MENU_FUNCTIONS)|
            {row['name'] for row in result['card_functions']}|set(FUNCTIONS)))
        self.assertTrue(set(original['tables']).issubset(set(TABLES)|{'rtos.mmu.context'}))
        for name in (*FUNCTIONS,*TABLES,'camera.ui.editor.page.build'):
            self.assertIn('"'+name+'",FX_BIND_',initializer)
        for address,_size,_digest in PINS.values():
            changed=bytearray(payload);changed[address-bindings.BASE]^=1
            with self.assertRaises(ValueError):verify(changed)
        for name in ('rtos.event.create','rtos.event.set','rtos.event.set-irq',
                     'rtos.event.clear-keep','rtos.event.wait','rtos.context.is-irq'):
            self.assertIn(name,bindings.RTOS_FUNCTIONS)
        self.assertEqual(len(result['menu_functions']), 3)
        self.assertEqual({r['name'] for r in result['card_functions']},{
            'camera.card.open','camera.card.read','camera.card.write','camera.card.seek64','camera.card.close','camera.card.rename',
            'camera.card.directory.open','camera.card.directory.next','camera.card.directory.close'})
        for row in result['card_functions']:
            self.assertIn('"'+row['name']+'",FX_BIND_FUNCTION',header)
        self.assertTrue(result['menu_functions'][0]['complete_function_verified'])
        self.assertFalse(result['menu_functions'][2]['complete_function_verified'])
        anchors = {row['name']: row['window_sha256'] for row in result['functions']}
        self.assertEqual(anchors, {**{k: v for k, v in anchors.items() if k not in bindings.ANCHOR_WINDOWS}, **bindings.ANCHOR_WINDOWS})

    def test_wrong_length_is_rejected(self):
        with self.assertRaises(ValueError):
            bindings.verify_application(Path(__file__))


if __name__ == '__main__':
    unittest.main()
