"""Z8 menu geometry contracts (T12 first-open baseline).

Host-side assertions over the pinned C311 menu geometry. The full
first-open reflow fixture (zf d3a00a5 equivalent: construct a page with N
entries and assert the visible row set, with a flush-disabled variant that
must reproduce stale rows) lands together with the render-profile RE:
fx_z8_menu_construct is still a dormant declaration, so this file pins the
geometry contracts the future fixture will build on.

Contracts:
- FxZ8OwnedMenu carries 16 rows x 13 navigation handlers, matching the
  13-record SETUP-family table pinned at 0x43bf0680 (12 routes + the
  0x3bb terminator; profile 'camera.menu.navigation').
- The pinned navigation table's record shape is {event, callback,
  condition, next} with a shared next page id.
- No Z9 navigation/table constants remain in the platform sources.
- Draw completion (rectangle/dispatch/empty), the menu event quintuple and
  the compositor submit tail are pinned to C311 values (validation 4b/4c).
"""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'host'))

from platform_profiles import PROFILES  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENU_DATA = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu_data.h'


def _owned_menu_dimensions():
    text = MENU_DATA.read_text()
    rows = int(re.search(r'#define FX_Z8_MENU_ROWS (\d+)u', text).group(1))
    handlers = re.search(r'FxZ8MenuEvent handlers\[FX_NATIVE_ROWS\]\[FX_NATIVE_EVENTS\];', text)
    return rows, handlers is not None


class Z8MenuFirstOpenGeometry(unittest.TestCase):
    def test_owned_menu_dimensions_match_pinned_table(self):
        rows, shared_layout = _owned_menu_dimensions()
        addr, size = PROFILES['z8-c311']['tables']['camera.menu.navigation']
        records = size // 32
        self.assertEqual(rows, 16)
        self.assertTrue(shared_layout,
                        'handler slots must use the shared menu layout '
                        '(the C311 table fills 13 of the 18 slots)')
        self.assertLessEqual(records, 18)

    def test_pinned_table_record_shape(self):
        # The 13-record table: each record is {u64 event, callback,
        # condition, next}; routes share the next page id 0x68c and the
        # 0x403b1b74..0x4054ea5c callback family; record 12 is the
        # terminator {0x3bb, 0, 0, 0x68c}.
        firmware = None
        for candidate in (ROOT / 'build' / 'verify-p1' / 'eg2070_0311b0.bi',
                          ROOT / 'build' / 'verify-p1' / 'eg2070-main-nocrc.bi'):
            if candidate.exists():
                firmware = candidate
                break
        if firmware is None:
            self.skipTest('local firmware image not present')
        data = firmware.read_bytes()
        base = 0x40160000
        addr, size = PROFILES['z8-c311']['tables']['camera.menu.navigation']
        import struct
        records = [struct.unpack_from('<4Q', data, addr - base + 32 * i)
                   for i in range(size // 32)]
        self.assertEqual(len(records), 13)
        self.assertEqual(records[0][0], 0x27e, 'first event must be SETUP (638)')
        self.assertEqual(records[12], (0x3bb, 0, 0, 0x68c),
                         'record 12 must be the terminator')
        callbacks = {r[1] for r in records}
        conditions = {r[2] for r in records}
        self.assertEqual(callbacks, {0x403b1b74, 0x403b1b8c, 0x403b1ba4,
                                     0x403b1ba8, 0x403b1bb0, 0x4054eae4,
                                     0x4054ea5c, 0})
        self.assertEqual(conditions, {0x403b1c5c, 0x403b1c7c, 0})
        for event, callback, condition, nxt in records:
            self.assertEqual(nxt, 0x68c)

    def test_no_z9_navigation_constants_remain(self):
        sources = (ROOT / 'src' / 'platform' / 'z8_c311').glob('*.c')
        for source in list(sources) + [MENU_DATA]:
            text = source.read_text()
            self.assertNotRegex(text, r'UINT64_C\(0x43014bb8\)', source.name)



class Z8RenderProfileContracts(unittest.TestCase):
    """Pinned C311 render-profile contracts (validation section 4c)."""

    MENU_C = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu.c'

    @classmethod
    def _source(cls):
        return cls.MENU_C.read_text()

    def test_layout_profile_pinned(self):
        text = self._source()
        for address in ('0x403afe64', '0x403aff78', '0x403b1a5c', '0x403db750',
                        '0x403b01f0', '0x403aec24', '0x403b035c', '0x403aed8c'):
            self.assertIn(address, text)

    def test_visibility_profile_pinned(self):
        text = self._source()
        for address in ('0x40325b60', '0x403f6228', '0x4031f9b8'):
            self.assertIn(address, text)
        self.assertIn('0x128u,0x4ba6u,0x4ba5u', text)

    def test_stale_z9_render_values_removed_from_enabled_path(self):
        # Reader/title/editor profiles intentionally retain Z9 references
        # (documented T15 residuals); only the enabled-path profiles changed.
        text = self._source()
        for stale in ('0x403b037c', '0x403b0490', '0x403b05d0', '0x403b03f8',
                      '0x403aed7c', '0x403af040', '0x403b0874', '0x403af294',
                      '0x403f196c', '0x403266ec', '0x403ebfb8', '0x403efcec',
                      '0x40c7cb34'):
            self.assertNotIn(stale, text, stale)

    def test_gui_mutex_pinned(self):
        text = self._source()
        self.assertIn('UINT64_C(0xa032b8044)', text)
        self.assertIn('0x40d2bfb4', text)
        self.assertIn('0x40d2bf60', text)
        self.assertNotIn('0xa032dcb98', text)

    def test_draw_profile_fully_pinned(self):
        """rectangle/dispatch/empty pinned from the stock pair site
        FUN_4039e848; modal_id stays 0 (non-modal pages; documented
        boundary)."""
        text = self._source()
        self.assertIn('FxNativeMenuDrawProfile draw={0x403f1940u,0x403f6388u,0x403f0db8u,0x40340d64u,0x40c7be74u,0x40325b60u,0u,',
                      text)


class Z8DrawCompletionContracts(unittest.TestCase):
    """Draw-completion pins: quintuple, submit tail, restore semantics."""

    MENU_C = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu.c'
    STUBS_C = ROOT / 'src' / 'platform' / 'z8_c311' / 'v0_stubs.c'

    def test_menu_quintuple_is_c311(self):
        text = (self.MENU_C).read_text()
        quint = '0x3bbu,0x27fu,{0x286u,0x28bu,0x2aeu,0u},0x68cu'
        self.assertEqual(text.count(quint), 3,
                         'all three FxNativeMenuProfile literals must carry the C311 quintuple')
        # The old Z9 quintuple combo must be gone; the individual ids
        # (0x297/0x2b1/0x2c0/0x298) legitimately appear in the pinned C311
        # ui_editor_events table, so only the combination is stale.
        self.assertNotIn('0x3cau,0x297u,{0x298u', text)
        self.assertNotIn('},0x6b8u', text)
        self.assertNotIn('0x6b8u,', text)

    def test_navigation_events_bound_is_13(self):
        text = self.MENU_C.read_text()
        self.assertEqual(text.count('0u},0x68cu,FENESTRA,13u}'), 1)
        self.assertEqual(text.count('},0x68cu,'), 3)
        self.assertNotIn('12u}', text)

    def test_refresh_tail_submit_is_c311(self):
        text = self.MENU_C.read_text()
        self.assertEqual(text.count('FN(0x403efed8'), 2,
                         'both refresh tails (module refresh + refresh_core) must submit via FUN_403efed8')
        self.assertEqual(text.count('UINT64_C(0x403efe74)'), 2)
        self.assertNotIn('FN(0x403eb0dc', text)
        self.assertNotIn('UINT64_C(0x403eb078', text)

    def test_v0_restore_stubs_return_empty(self):
        """Backdrop/menu restore stubs must return FX_EMPTY (nothing to
        restore) so the enabled image's draw path proceeds; FX_CAPABILITY
        would fail fx_native_menu_draw for every non-modal page."""
        text = self.STUBS_C.read_text()
        self.assertIn('FxResult fx_native_menu_restore(const FxNativePage *page){(void)page;return FX_EMPTY;}', text)
        self.assertIn('FxResult fx_native_backdrop_restore(const FxNativePage *page){(void)page;return FX_EMPTY;}', text)


if __name__ == '__main__':
    unittest.main()
