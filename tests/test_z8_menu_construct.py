"""Z8 menu construction contracts (T12 construct port).

Source contracts plus a virt-machine fixture that exercises
fx_z8_menu_construct against the pinned 13-record navigation table shape:

- construct replaces the activation trio {0x286,0x28b,0x2ae} in every row's
  handler table with the entry's action and keeps the stock cursor/back
  routes untouched (zf d3a00a5 equivalent at the construct level);
- the 0x3bb terminator terminates the scan (13 records copied);
- a table without the terminator (the old 12-record Z9 shape) must fail
  with FX_INTEGRITY instead of walking off the table.
"""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'host'))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from z8_unit_support import build_and_run  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENU_C = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu.c'
MENU_DATA = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu_data.h'
CONSTRUCT_C = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu_construct.c'
BUILDER = ROOT / 'host' / 'build_z8_core.py'
FIXTURE = ROOT / 'tests' / 'z8_menu_construct_fixture.c'
FIXTURE_LD = ROOT / 'tests' / 'z8_menu_construct_fixture.ld'


class Z8MenuConstructSources(unittest.TestCase):
    def test_construct_wrapper_exists(self):
        self.assertTrue(CONSTRUCT_C.exists())
        text = CONSTRUCT_C.read_text()
        self.assertIn('FxResult fx_z8_menu_construct(', text)
        self.assertIn('0x3bbu, 0x27fu, {0x286u, 0x28bu, 0x2aeu, 0u}, 0x68cu,', text)

    def test_install_constructs_in_enabled_config(self):
        text = MENU_C.read_text()
        self.assertIn('construct_pages(s, sp)', text)
        # The enabled install publishes once, at the end (z9 pattern).
        self.assertIn('#ifdef FX_Z8_HEADLESS', text)
        # The pump integration must exist for both configurations.
        self.assertIn('FxNativeUiDispatch *fx_native_ui_dispatch_domain(void)', text)

    def test_builder_supports_menu_enabled(self):
        text = BUILDER.read_text()
        self.assertIn('--menu-enabled', text)

    def test_module_ui_templates_use_stock_row(self):
        """I-2 fix: the module-UI/apps/management profile literals must use
        the construct-copied stock row (&s->rows[2]); &s->rows[44] is zeroed
        scratch on the 1-row C311 hub page."""
        text = MENU_C.read_text()
        self.assertNotIn('&s->rows[44]', text)
        self.assertEqual(text.count('&s->rows[2]'), 3)

    def test_shared_routes_use_bounded_link(self):
        """M-3: the shared route rewriter must consume the profile's
        navigation record count (C311 tables are 13 records; the legacy
        18-record bound only holds by terminator luck)."""
        text = (ROOT / 'src' / 'native_ui_routes.c').read_text()
        self.assertNotIn('result=fx_native_menu_link(program->events', text)
        self.assertIn('fx_native_menu_link_bounded(program->events', text)
        self.assertIn('u->profile.navigation_events', text)

    def test_fixture_pins_real_table_shape(self):
        text = FIXTURE.read_text()
        self.assertIn('{0x3bb,0,0,0x68c}', text)
        self.assertIn('{0x27f,0x403b1bb0,0x403b1c5c,0x68c}', text)


class Z8MenuConstructFixture(unittest.TestCase):
    """Runs fx_z8_menu_construct in the isolated virt machine."""

    def test_construct_rewrites_activation_trio(self):
        result = build_and_run(
            'tests/z8_menu_construct_fixture.c',
            ['src/platform/z8_c311/menu_construct.c', 'src/native_menu.c'],
            'CONSTRUCT OK', ld='tests/z8_menu_construct_fixture.ld')
        if result is None:
            self.skipTest('toolchain or emulator unavailable')
        rc, output, passed = result
        self.assertEqual(rc, 0, output)
        self.assertTrue(passed, output)


if __name__ == '__main__':
    unittest.main()
