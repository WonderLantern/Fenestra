"""Offline Z8 platform-profile pinning tests: no firmware material required."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
from platform_profiles import PROFILES, UI_CONDITION_ABI

Z8_FUNCTIONS = {
    'rtos.memory.allocate', 'rtos.memory.release',
    'camera.card.open', 'camera.card.read', 'camera.card.close',
    'camera.card.seek64', 'camera.card.write', 'camera.card.rename',
    'camera.menu.page.get', 'camera.menu.text.get',
    'camera.menu.navigate', 'camera.menu.open',
}


class Z8ProfileOfflineTests(unittest.TestCase):
    def test_z8_profile_pinned_fields(self):
        p = PROFILES['z8-c311']
        self.assertEqual(p['model'], 'Nikon Z8')
        self.assertEqual(p['version'], 'C 3.11')
        self.assertEqual(p['bytes'], 81807711)
        self.assertEqual(p['sha256'], p['module_identity'])
        self.assertEqual(p['module_identity'],
                         'f85e0138e2f5e660ece8560de62e0d12cb795e0e14983e50f20b61dce906c46c')
        self.assertEqual(set(p['functions']), Z8_FUNCTIONS)
        for name, address in p['functions'].items():
            self.assertTrue(0x40160000 <= address < 0x41000000, name)

    def test_z8_absent_from_ui_condition_abi(self):
        # The Z8 UI condition ABI is not pinned; guard against reusing ZR/Z9 values.
        self.assertNotIn('z8-c311', UI_CONDITION_ABI)

    def test_fw_list_binding_anchored_in_code_and_doc(self):
        menu = (ROOT / 'src/platform/z8_c311/menu.c').read_text(encoding='utf-8')
        self.assertIn('0x402db09c', menu)  # FW_LIST callback-enumeration entry
        doc = (ROOT / 'docs/validation-z8-c311.md').read_text(encoding='utf-8')
        self.assertIn('0x80000030', doc)   # directory end sentinel


if __name__ == '__main__':
    unittest.main()
