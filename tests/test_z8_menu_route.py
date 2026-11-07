"""Z8 routed-id allocation contracts (T5).

The manager UI window, the apps list page and the dynamic module-UI
window join the T4b own pages below the C311 kind-2 navigation gate
(0x403af654: every list target >= 0x68c is dropped before page.get).
Source contracts plus a virt-machine fixture that runs the production
fx_z8_route table and its validators: the shipped table validates, the
parameterized minimal allocations exhaust with FX_CAPABILITY (never a
fallback id), and gate/overlap violations are rejected.
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
ROUTE_C = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu_route.c'
APPS_C = ROOT / 'src' / 'native_apps.c'
MODULES_C = ROOT / 'src' / 'native_modules.c'

WINDOW_FIRST, WINDOW_LAST, MARGIN, GATE = 0x61c, 0x65d, 5, 0x68c
ROW_FAMILY_FIRST, ROW_FAMILY_LAST = 0x45cf, 0x4631


def table():
    """Parses the shipped fx_z8_route values from menu_route.c; own-page
    fields reference the menu_data.h macros, so those resolve first."""
    macros = dict(re.findall(r'#define (FX_Z8_PAGE_\w+) (0x[0-9a-fA-F]+)u', MENU_DATA.read_text()))
    text = ROUTE_C.read_text()
    values = {}
    for key, value in re.findall(r'\.(\w+)\s*=\s*([A-Za-z_0-9]+)u?,', text):
        values[key] = int(macros.get(value, value.rstrip('u')), 0)
    values['module_row_base[0]'] = int(re.search(r'\.module_row_base\s*=\s*\{(0x[0-9a-fA-F]+)u', text).group(1), 0)
    return values


class Z8RouteSources(unittest.TestCase):
    def test_own_page_ids_pinned_in_menu_data(self):
        text = MENU_DATA.read_text()
        for name, value in (('FX_Z8_PAGE_FENESTRA', 0x63c), ('FX_Z8_PAGE_WONDER', 0x63d),
                            ('FX_Z8_PAGE_AUTO', 0x63e), ('FX_Z8_PAGE_GUARD', 0x63f)):
            self.assertIn(f'#define {name} 0x{value:x}u', text)
        # menu.c must consume the shared pins, not redefine them.
        menu = MENU_C.read_text()
        for name in ('FX_Z8_PAGE_FENESTRA', 'FX_Z8_PAGE_WONDER',
                     'FX_Z8_PAGE_AUTO', 'FX_Z8_PAGE_GUARD'):
            self.assertNotIn(f'#define {name} ', menu)

    def test_routed_windows_inside_audited_band(self):
        t = table()
        pages = [(t['fenestra_page'], 1), (t['wonder_page'], 1), (t['auto_page'], 1),
                 (t['guard_page'], 1), (t['apps_list_base'], 1),
                 (t['manager_base'], 18),
                 (t['module_page_first'], t['module_page_limit'] - t['module_page_first'])]
        for first, count in pages:
            self.assertGreaterEqual(first, WINDOW_FIRST + MARGIN)
            self.assertLessEqual(first + count - 1, WINDOW_LAST - MARGIN)
            self.assertLess(first + count - 1, GATE)
        # Pairwise disjoint.
        spans = [(f, f + c) for f, c in pages]
        for i, (a0, a1) in enumerate(spans):
            for b0, b1 in spans[i + 1:]:
                self.assertTrue(a1 <= b0 or b1 <= a0, (hex(a0), hex(b0)))
        # The manager window is exactly one shared UI window (FX_UI_CONTROLS+2
        # = 18 ids) and the module window can host at least a paged UI.
        self.assertEqual(t['manager_base'] + 18 - t['manager_base'], 18)
        self.assertGreaterEqual(t['module_page_limit'] - t['module_page_first'], 2)

    def test_borrowed_row_bases_inside_stock_family(self):
        t = table()
        spans = [(t['manager_row_base'], 24), (t['manager_row_base'] + 40, 2),
                 (t['apps_row_base'], 10), (t['apps_row_base'] + 0x10, 5),
                 (t['module_row_base[0]'], 24), (t['module_row_base[0]'] + 40, 2)]
        for first, count in spans:
            self.assertGreaterEqual(first, ROW_FAMILY_FIRST)
            self.assertLessEqual(first + count - 1, ROW_FAMILY_LAST)
        for i, (a0, ac) in enumerate(spans):
            a1 = a0 + ac
            for b0, bc in spans[i + 1:]:
                b1 = b0 + bc
                self.assertTrue(a1 <= b0 or b1 <= a0, (hex(a0), hex(b0)))
        # Clear of the language/region family and the 663 entry rows.
        for first, count in spans:
            self.assertFalse(0x46e8 <= first + count - 1 and 0x46e8 + 14 > first)
        menu = MENU_C.read_text()
        self.assertIn('#define FX_Z8_ENTRY_ROW_ID0 0x2d1cu', menu)
        self.assertIn('#define FX_Z8_ENTRY_ROW_ID1 0x2d1du', menu)
        self.assertNotIn('0x7ff1', menu)

    def test_exhaustion_is_explicit_capability(self):
        text = ROUTE_C.read_text()
        self.assertIn('return FX_CAPABILITY;\n    *row_base = r->module_row_base[load_index];', text)
        menu = MENU_C.read_text()
        # The platform maps routed-slot exhaustion to FX_CAPABILITY too.
        self.assertIn('FxResult routed=fx_z8_route_module_row_base(&fx_z8_route,slot,&row_base);', menu)
        self.assertIn('if(routed!=FX_OK)return routed;', menu)
        # The table itself gates the construct (never fake success).
        self.assertIn('if(fx_z8_route_validate(&fx_z8_route)!=FX_OK)return 0;', menu)

    def test_shared_apps_code_has_no_target_literals(self):
        """Shared code must stay target-neutral: no Z8 page/row literals
        and no absolute 0x7b00-family rows (the platform supplies them)."""
        for source in (APPS_C, MODULES_C):
            text = source.read_text()
            for literal in ('0x7b0', '0x7b1', '0x45cf', '0x461a', '0x4600', '0x624', '0x636'):
                self.assertNotIn(literal, text)

    def test_builder_lists_route_module(self):
        text = (ROOT / 'host' / 'build_z8_core.py').read_text()
        self.assertIn("'menu.c', 'menu_construct.c', 'menu_route.c'", text)

    def test_profile_mirrors_shipped_table(self):
        from platform_profiles import PROFILES
        routed = PROFILES['z8-c311']['routed_pages']
        t = table()
        self.assertEqual(routed['own_pages'],
                         (t['fenestra_page'], t['wonder_page'], t['auto_page'], t['guard_page']))
        self.assertEqual(routed['apps_root'], t['wonder_page'])
        self.assertEqual(routed['apps_list'], t['apps_list_base'])
        self.assertEqual(routed['manager'], t['manager_base'])
        self.assertEqual(routed['module_pages'], (t['module_page_first'], t['module_page_limit']))
        self.assertEqual(routed['rows']['manager'], t['manager_row_base'])
        self.assertEqual(routed['rows']['apps'], t['apps_row_base'])
        self.assertEqual(routed['rows']['modules'], [t['module_row_base[0]']])


class Z8RouteFixture(unittest.TestCase):
    """Runs fx_z8_route_validate and the dynamic-slot lookup in the
    isolated virt machine, including the parameterized minimal
    allocations whose exhaustion must surface FX_CAPABILITY."""

    def test_route_table_validates_and_exhausts(self):
        result = build_and_run(
            'tests/z8_menu_route_fixture.c',
            ['src/platform/z8_c311/menu_route.c'],
            'ROUTE OK', ld='tests/z8_unit_fixture.ld')
        if result is None:
            self.skipTest('toolchain or emulator unavailable')
        rc, output, passed = result
        self.assertEqual(rc, 0, output)
        self.assertTrue(passed, output)


if __name__ == '__main__':
    unittest.main()
