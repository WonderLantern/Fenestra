"""Offline checks for the pinned Z8 native-menu bindings (T12).

Pure host-side assertions: the profile must carry the pinned menu trio,
the text-bank roots and the navigation table recorded in
docs/validation-z8-c311.md; menu_data.h must no longer carry the Z9
navigation reference; and the font-site profile must expose the 13 C311
request pairs with the C311 probe geometry. No firmware image is required.
"""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'host'))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'emulation'))

from platform_profiles import PROFILES  # noqa: E402
import native_font_sites  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENU_DATA = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu_data.h'


class Z8MenuProfileOffline(unittest.TestCase):
    def test_menu_trio_pinned(self):
        functions = PROFILES['z8-c311']['functions']
        self.assertEqual(functions['camera.menu.page.get'], 0x403db694)
        self.assertEqual(functions['camera.menu.text.get'], 0x4032e130)
        self.assertEqual(functions['camera.menu.navigate'], 0x4039e810)
        self.assertEqual(functions['camera.menu.open'], 0x403ad6d8)

    def test_text_bank_roots_pinned(self):
        tables = PROFILES['z8-c311']['tables']
        self.assertEqual(tables['camera.text.count'], (0xa0281cd5c, 4))
        self.assertEqual(tables['camera.text.pool-root'], (0xa02f2ac38, 8))
        self.assertEqual(tables['camera.text.font-root'], (0xa02f2ac40, 8))
        self.assertEqual(tables['camera.text.offsets-root'], (0xa02f2ac48, 8))
        self.assertEqual(tables['camera.text.attributes-root'], (0xa02f2ac50, 8))
        # The Z9 reference bank cluster must not reappear.
        for name, (addr, _n) in tables.items():
            self.assertNotEqual(addr, 0xa02f4ab28, name)
            self.assertNotEqual(addr, 0xa02f4ab38, name)

    def test_navigation_table_replaces_z9_reference(self):
        tables = PROFILES['z8-c311']['tables']
        addr, size = tables['camera.menu.navigation']
        self.assertEqual(addr, 0x43bf0680)
        self.assertEqual(size, 12 * 32)
        text = MENU_DATA.read_text()
        self.assertIn('#define FX_Z8_MENU_NAVIGATION UINT64_C(0x43bf0680)', text)
        self.assertNotRegex(text, r'#define FX_Z8_MENU_NAVIGATION UINT64_C\(0x43014bb8\)')
        self.assertNotRegex(text, r'#define FX_Z8_MENU_EVENTS\s+18')
        self.assertNotIn('UINT64_C(0xa032d5de8)', (ROOT / 'src' / 'platform' / 'z8_c311' / 'menu.c').read_text())

    def test_font_sites_geometry(self):
        sites = native_font_sites.sites('z8-c311')
        enters = [a for a, n in sites.items() if n.endswith('_enter')]
        leaves = [a for a, n in sites.items() if n.endswith('_leave')]
        self.assertEqual(len(enters), 13)
        self.assertEqual(len(leaves), 13)
        # C311 leaves sit 28 bytes after their enter, matching the Z9 geometry.
        for enter in enters:
            self.assertIn(enter + 28, leaves)
        probes = [a for a, n in sites.items() if n.endswith('_line_metrics')]
        self.assertEqual(probes, [0x402eeb28])
        profile = native_font_sites.PROFILES['z8-c311']
        self.assertEqual(profile['lock'], 0x402e5550)
        self.assertEqual(profile['unlock'], 0x402e5564)
        self.assertEqual(profile['measure'], 0x4088bdac)
        self.assertEqual(profile['set_layout'], 0x4088c620)

    def test_z9_font_values_not_reused(self):
        profile = native_font_sites.PROFILES['z8-c311']
        z9 = native_font_sites.PROFILES['z9-c532']
        for field in ('lock', 'unlock', 'measure', 'set_layout', 'line_probe'):
            self.assertNotEqual(profile[field], z9[field], field)


if __name__ == '__main__':
    unittest.main()
