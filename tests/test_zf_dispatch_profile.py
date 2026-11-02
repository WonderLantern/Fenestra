"""Reject wrong Zf dispatch edges independently of whole-image identity."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
import zf_c301_dispatch_profile as profile
import native_ui_dispatch_sites as edges


class ZfDispatchProfileTests(unittest.TestCase):
    def test_zf_render_selector_and_value_child_are_not_z9_constants(self):
        data=bytearray(0x403b8e70-edges.BASE)
        struct.pack_into('<I',data,0x403b8e68-edges.BASE,0x321f0be0)
        struct.pack_into('<I',data,0x403b8b10-edges.BASE,0x52800001|(0x4917<<5))
        for pc,target in [(0x403b8e6c,0x403a5a00),(0x403b8b18,0x403fb8b4)]:
            data[pc-edges.BASE:pc-edges.BASE+4]=edges.call(pc,target)
        with patch.object(profile,'RENDER_WINDOWS',{}):
            self.assertEqual(profile.verify_render_layout(data)['value_child'],0x4917)
            for pc,replacement in [(0x403b8e68,0x52800140),(0x403b8b10,0x52800001|(0x4bec<<5))]:
                bad=bytearray(data);struct.pack_into('<I',bad,pc-edges.BASE,replacement)
                with self.subTest(pc=hex(pc)),self.assertRaises(ValueError):profile.verify_render_layout(bad)

    def test_gui_call_and_object_checks_are_independent_of_hash(self):
        data=bytearray(0x404003e8-edges.BASE)
        for pc,target in [(0x404003b4,0x40d31444),(0x404003c4,0x4033105c),(0x404003d0,0x40d313f0)]:
            data[pc-edges.BASE:pc-edges.BASE+4]=edges.call(pc,target)
        for index in range(4):
            word=(0xd2800000 if not index else 0xf2800000)|(index<<21)|(((0xa03408b68>>(16*index))&65535)<<5)|22
            struct.pack_into('<I',data,0x404003a0-edges.BASE+index*4,word)
        with patch.object(profile,'GUI_WINDOWS',{}):
            result=profile.verify_gui(data)
            self.assertEqual(result['object'],'0xa03408b68')
            self.assertFalse(result['native_ui_flush_implemented'])
            for pc in [0x404003b4,0x404003c4,0x404003d0,0x404003a0]:
                bad=bytearray(data);bad[pc-edges.BASE]^=1
                with self.subTest(pc=hex(pc)),self.assertRaises(ValueError):profile.verify_gui(bad)

    def fixture(self):
        p = dict(edges.PROFILES['zf-c301'])
        data = bytearray(max(at for pair in p['loops'] for at in pair) - edges.BASE + 8)
        def put(address, raw):
            data[address - edges.BASE:address - edges.BASE + len(raw)] = raw
        for wait, poll in p['loops']:
            put(wait - 8, edges.call(wait - 8, p['event_group']))
            put(wait, edges.call(wait, p['wait']))
            put(wait + 4, bytes.fromhex('e203002a'))
            put(wait + 16, edges.call(wait + 16, p['event_wait']))
            put(poll - 4, struct.pack('<I', 0x910003e0))
            put(poll, edges.call(poll, p['poll']))
        put(p['current_task'], bytes.fromhex('8ad038d54a0140f9'))
        a = p['wake_reference']
        put(a, edges.call(a, p['event_group']) + bytes.fromhex('e1030b32') + edges.call(a + 8, p['event_set']))
        a = p['event_set']
        put(a, bytes.fromhex('000040f9400000b4'))
        put(a + 8, struct.pack('<I', 0x14000000 | (((p['event_set_native']-(a+8))//4)&0x3ffffff)))
        put(a + 12, bytes.fromhex('c0035fd6'))
        return p, data

    def verify(self, p, data):
        p['sha256'] = hashlib.sha256(data).hexdigest()
        with patch.dict(edges.PROFILES, {'zf-c301': p}):
            return edges.verify_original('zf-c301', data)

    def test_original_edges_have_ten_explicit_calls_and_no_nested_replacement(self):
        p, data = self.fixture()
        result = self.verify(p, data)
        self.assertEqual(result['main_loop_calls'], 10)
        self.assertFalse(result['nested_timer_entry_replaced'])
        self.assertEqual(len(edges.sites('zf-c301')), 10)
        self.assertTrue(p['experimental'])
        self.assertEqual(profile.PREIMAGES['poll'][:2], (p['poll'], 304))

    def test_each_misdirected_call_is_rejected_even_with_matching_image_hash(self):
        p, data = self.fixture()
        for address in edges.sites('zf-c301'):
            with self.subTest(address=hex(address)):
                bad = bytearray(data)
                bad[address-edges.BASE:address-edges.BASE+4] = edges.call(address, p['event_group'])
                with self.assertRaisesRegex(ValueError, 'main loop call differs'):
                    self.verify(p, bad)

    def test_task_context_and_coalescing_wake_abi_are_required(self):
        p, data = self.fixture()
        for address, reason in [(p['current_task'], 'TPIDR'), (p['wake_reference'] + 4, 'wake producer')]:
            with self.subTest(address=hex(address)):
                bad = bytearray(data)
                bad[address-edges.BASE] ^= 1
                with self.assertRaisesRegex(ValueError, reason):
                    self.verify(p, bad)


if __name__ == '__main__':
    unittest.main()
