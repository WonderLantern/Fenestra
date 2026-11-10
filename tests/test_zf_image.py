"""Regression: executable placement base is not a callable Zf startup entry."""
import struct
import sys
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import build_zf_image as core


class ZfImageTests(unittest.TestCase):
    def test_ptp_hooks_check_preimages_and_preserve_displaced_tail(self):
        original,constants=self.fixture()
        entries={name:(address,core.sha(bytes(16))) for name,address in
                 [('lookup',0x1800),('session',0x1900),('operations',0x1a00)]}
        symbols={'fx_zf_system_dispatch':0x1440,'fx_zf_ptp_lookup':0x1450,
                 'fx_zf_ptp_session':0x1460,'fx_zf_ptp_operations':0x1470}
        with patch.multiple(core,**constants),patch.object(core,'PTP_ENTRIES',entries):
            candidate,changes=core.patch_image(original,bytes(128),symbols,0x1440,native_ptp=True)
            self.assertEqual([c['name'] for c in changes][-3:],['ptp-lookup','ptp-session','ptp-operations'])
            for name,(address,_) in entries.items():
                at=address-constants['BASE']
                self.assertEqual(candidate[at:at+4],core.branch(address,symbols['fx_zf_ptp_'+name]))
                self.assertEqual(candidate[at+4:at+16],original[at+4:at+16])
            entries['session']=(0x1900,'0'*64)
            with self.assertRaisesRegex(ValueError,'PTP entry window differs'):
                core.patch_image(original,bytes(128),symbols,0x1440,native_ptp=True)

    def test_owner_layout_probes_use_the_linked_owner_feature_macros(self):
        self.assertEqual(core.owner_layout_defines(), [])
        self.assertEqual(core.owner_layout_defines(module_stage_probe=True),
                         ['-DFX_ZF_EXPERIMENTAL_MODULE_STAGE_PROBE=1'])
        self.assertEqual(core.owner_layout_defines(module_executor=True),
                         ['-DFX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1','-DFX_ZF_MODULE_WORKERS=1'])
        self.assertEqual(core.owner_layout_defines(True, True), [
            '-DFX_ZF_EXPERIMENTAL_MODULE_STAGE_PROBE=1',
            '-DFX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1','-DFX_ZF_MODULE_WORKERS=1'])

    def test_dispatch_calls_require_menu_capture_and_preserve_original_helpers(self):
        original,constants=self.fixture()
        with self.assertRaisesRegex(ValueError,'requires the checked menu'):
            core.patch_image(original,bytes(128),{},0,ui_dispatch=True)
        hooks={name:dict(address=address,bytes=20,sha256=core.sha(bytes(20)))
               for name,address in [('page',0x1800),('text',0x1900),('navigate',0x1a00)]}
        symbols={'fx_zf_system_dispatch':0x1440,'fx_zf_menu_page':0x1450,'fx_zf_menu_text':0x1460,
            'fx_zf_menu_navigate':0x1470,'fx_native_ui_wait_native':0x1444,'fx_native_ui_poll_native':0x1448}
        with patch.multiple(core,**constants),patch.object(core,'MENU_HOOKS',hooks),\
             patch.object(core,'verify_ui_sites',return_value={}) as verify,\
             patch.object(core,'ui_sites',return_value={0x1b00:'fx_native_ui_wait_native',0x1b08:'fx_native_ui_poll_native'}):
            candidate,changes=core.patch_image(original,bytes(128),symbols,0x1440,True,True)
            verify.assert_called_once_with('zf-c301',original[:-2])
            self.assertEqual(len(changes),7)
            self.assertEqual(candidate[0xb00:0xb04],core.ui_call(0x1b00,0x1444))
            self.assertEqual(candidate[0xb08:0xb0c],core.ui_call(0x1b08,0x1448))
            self.assertEqual(candidate[0xb04:0xb08],original[0xb04:0xb08])

    def test_menu_patches_require_pinned_functions_and_named_targets(self):
        original,constants=self.fixture()
        hooks={name:dict(address=address,bytes=20,sha256=core.sha(bytes(20)))
               for name,address in [('page',0x1800),('text',0x1900),('navigate',0x1a00)]}
        symbols={'fx_zf_system_dispatch':0x1440,'fx_zf_menu_page':0x1450,
                 'fx_zf_menu_text':0x1460,'fx_zf_menu_navigate':0x1470}
        with patch.multiple(core,**constants),patch.object(core,'MENU_HOOKS',hooks):
            candidate,changes=core.patch_image(original,bytes(range(128)),symbols,0x1440,True)
            self.assertEqual([c['name'] for c in changes],['core','sysctl','menu-page','menu-text','menu-navigate'])
            for name,spec in hooks.items():
                at=spec['address']-constants['BASE']
                self.assertEqual(candidate[at:at+4],core.branch(spec['address'],symbols['fx_zf_menu_'+name]))
                self.assertEqual(candidate[at+4:at+20],original[at+4:at+20])
            hooks['text']['sha256']='0'*64
            with self.assertRaisesRegex(ValueError,'window differs'):
                core.patch_image(original,bytes(128),symbols,0x1440,True)

    def fixture(self):
        original = bytes(4096)
        constants = dict(BASE=0x1000, EG_SIZE=len(original), EG_SHA256=core.sha(original),
                         SYSCTL_DISPATCH=0x1c00, MAPPED_CODE_VA=0x1400, MAPPED_CODE_BYTES=512,
                         PREFIX_SHA256=core.sha(bytes(16)), SPAN_SHA256=core.sha(bytes(268)))
        return original, constants

    def test_hook_branches_to_named_wrapper_and_preserves_every_other_byte(self):
        original, constants = self.fixture()
        code = bytes(range(128))
        with patch.multiple(core, **constants):
            candidate, changes = core.patch_image(original, code, {'fx_zf_system_dispatch':0x1440}, 0x1440)
        word, = struct.unpack_from('<I', candidate, 0xc00)
        imm = word & 0x03ffffff
        if imm & (1 << 25): imm -= 1 << 26
        self.assertEqual(0x1c00 + (imm << 2), 0x1440)
        self.assertNotEqual(0x1440, constants['MAPPED_CODE_VA'])
        restored = bytearray(candidate)
        for change in changes:
            at, size = change['offset'], change['bytes']
            restored[at:at+size] = original[at:at+size]
        self.assertEqual(restored, original)
        self.assertEqual(candidate[0xc04:], original[0xc04:])

    def test_first_scene_function_cannot_masquerade_as_startup_entry(self):
        original, constants = self.fixture()
        with patch.multiple(core, **constants):
            with self.assertRaisesRegex(ValueError, 'ELF entry must name'):
                core.patch_image(original, bytes(128), {'fx_zf_system_dispatch':0x1440}, 0x1400)
            with self.assertRaisesRegex(ValueError, 'ELF entry must name'):
                core.patch_image(original, bytes(128), {}, 0x1400)

    def test_input_change_and_out_of_image_wrapper_are_rejected(self):
        original, constants = self.fixture()
        with patch.multiple(core, **constants):
            with self.assertRaisesRegex(ValueError, 'identity'):
                core.patch_image(b'x'+original[1:], bytes(128), {'fx_zf_system_dispatch':0x1440}, 0x1440)
            with self.assertRaisesRegex(ValueError, 'outside'):
                core.patch_image(original, bytes(128), {'fx_zf_system_dispatch':0x1500}, 0x1500)


if __name__ == '__main__': unittest.main()
