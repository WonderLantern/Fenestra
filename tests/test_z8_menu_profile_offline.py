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
        # 13 records = 12 routes + the {0x3bb,0,0,0x68c} terminator; the
        # quintuple's terminal id and navigation_events bound both consume it.
        self.assertEqual(size, 13 * 32)
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


class Z8MenuIdsOffline(unittest.TestCase):
    """T2/T4b id-space pins: borrowed row ids and the routed page ids.

    The wrench-tab kind-2 navigation gate (FUN_403af53c: cmp w22,w20 @
    0x403af654 + b.ls @0x403af658) drops every list target >= 0x68c
    before page.get runs, so navigable own pages live below the gate in
    the audited free window, and row ids stay registered stock ids
    (T12 P3: unregistered ids abort the whole row rebuild).
    """

    MENU_C = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu.c'

    def test_pinned_tables_t2_t4b(self):
        tables = PROFILES['z8-c311']['tables']
        self.assertEqual(tables['camera.menu.row-events.663'], (0x43d745c0, 59 * 32))
        self.assertEqual(tables['camera.menu.rows.language-region'], (0x43f142dc, 14 * 44))
        self.assertEqual(tables['camera.menu.rows.manager'], (0x43cf133c, 99 * 44))
        self.assertEqual(tables['camera.menu.descriptor-templates'], (0x44d28110, 0x4c3 * 0x50))

    def test_routed_page_ids_below_gate(self):
        text = MENU_DATA.read_text()
        for name, value in (('FX_Z8_PAGE_FENESTRA', 0x63c), ('FX_Z8_PAGE_WONDER', 0x63d),
                            ('FX_Z8_PAGE_AUTO', 0x63e), ('FX_Z8_PAGE_GUARD', 0x63f)):
            self.assertIn(f'#define {name} 0x{value:x}u', text)
        # All routed ids must sit inside the audited free window [0x61c,
        # 0x65d] and strictly below the kind-2 gate bound 0x68c.
        for value in (0x63c, 0x63d, 0x63e, 0x63f):
            self.assertTrue(0x61c <= value <= 0x65d)
            self.assertLess(value, 0x68c)

    def test_extension_interval_keeps_namespace_bound(self):
        # The NULL interval and the navigate-hook interval must stay on
        # the shared 0x7000-family constant: widening them past stock
        # dialog templates crashed the t4a s2 probe round.
        text = self.MENU_C.read_text()
        self.assertIn('id >= FX_NATIVE_FENESTRA_PAGE && id < FX_NATIVE_EXTENSION_PAGE_LIMIT', text)
        self.assertIn('page>=FX_NATIVE_FENESTRA_PAGE && page<FX_NATIVE_EXTENSION_PAGE_LIMIT', text)
        for stale in ('id >= FENESTRA &&', 'id>=FENESTRA &&', 'page>=FENESTRA &&'):
            self.assertNotIn(stale, text)

    def test_borrowed_row_ids_pinned(self):
        text = self.MENU_C.read_text()
        rows = [('FX_Z8_CORE_ACTIVATE_ID', 0x46e8), ('FX_Z8_CORE_STATUS_ID', 0x46e9),
                ('FX_Z8_CORE_MODULES_ID', 0x46ea), ('FX_Z8_CORE_AUTOSTART_ID', 0x46eb),
                ('FX_Z8_CORE_GUARDFLAG_ID', 0x46ec), ('FX_Z8_CORE_USB_ID', 0x46ed),
                ('FX_Z8_CORE_BACK_ID', 0x46ee), ('FX_Z8_AUTOMENU_ON_ID', 0x46f1),
                ('FX_Z8_AUTOMENU_OFF_ID', 0x46f2), ('FX_Z8_GUARDMENU_ROW_ID', 0x46f3),
                ('FX_Z8_GUARDMENU_CLEAR_ID', 0x46f4)]
        for name, value in rows:
            self.assertIn(f'#define {name} 0x{value:x}u', text)
        # Manager/apps/module row bases live in the route table (T5); the
        # 0x46ef/0x46f0 wonder rows left the language family with the
        # static page (the apps root now owns the wonder id).
        route = (ROOT / 'src' / 'platform' / 'z8_c311' / 'menu_route.c').read_text()
        for name, value in (('.manager_row_base', 0x4600), ('.apps_row_base', 0x461a)):
            self.assertIn(f'{name} = 0x{value:x}u', route)
        self.assertIn('.module_row_base = {0x45cfu}', route)
        # Entry rows stay the page-25 borrow; the 0x7ff0 family must not
        # return (unregistered: aborts the row rebuild).
        self.assertIn('#define FX_Z8_ENTRY_ROW_ID0 0x2d1cu', text)
        self.assertIn('#define FX_Z8_ENTRY_ROW_ID1 0x2d1du', text)
        self.assertNotIn('0x7ff1', text)

    def test_select_trio_rebind_shape(self):
        # link_row_table must rewrite ONLY the select trio {0x286,0x28b,
        # 0x2ae} of the full stock 663 row table, keeping every other
        # record byte-exact (the wrench consumer walks the whole family).
        text = self.MENU_C.read_text()
        self.assertIn('if(event!=0x286u && event!=0x28bu && event!=0x2aeu)continue;', text)
        self.assertIn('FX_Z8_ROW_EVENT_RECORDS 59u', text)
        # The entry rows keep the stock select callback (plain ret).
        self.assertIn('link_row_table(s->entry_events[r],0u,r?WONDER:FENESTRA);', text)
        # Row-table pointers must address the r-th TABLE (a record-shifted
        # view resolves the ok trio of the wrong row: t4b-diag-auto-r1).
        self.assertIn('menu->events[r]=(uintptr_t)tables[first+r];', text)
        self.assertNotIn('&tables[first][r]', text)
        # Own-row navigation targets use the routed page ids / stock stays.
        self.assertIn('(uintptr_t)noop,FX_Z8_PAGE_AUTO}', text)
        self.assertIn('(uintptr_t)noop,FX_Z8_PAGE_GUARD}', text)


if __name__ == '__main__':
    unittest.main()
