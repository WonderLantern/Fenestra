"""Z8 menu geometry contracts (T12 first-open baseline).

Host-side assertions over the pinned C311 menu geometry. The full
first-open reflow fixture (zf d3a00a5 equivalent: construct a page with N
entries and assert the visible row set, with a flush-disabled variant that
must reproduce stale rows) lands together with the render-profile RE:
fx_z8_menu_construct is still a dormant declaration, so this file pins the
geometry contracts the future fixture will build on.

Contracts:
- FxZ8OwnedMenu carries 16 rows x 12 navigation handlers, matching the
  12-record SETUP-family table pinned at 0x43bf0680 (profile
  'camera.menu.navigation').
- The pinned navigation table's record shape is {event, callback,
  condition, next} with a shared next page id.
- No Z9 navigation/table constants remain in the platform sources.
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
    handlers = re.search(r'FxZ8MenuEvent handlers\[FX_Z8_MENU_ROWS\]\[(\d+)\];', text)
    return rows, int(handlers.group(1))


class Z8MenuFirstOpenGeometry(unittest.TestCase):
    def test_owned_menu_dimensions_match_pinned_table(self):
        rows, handlers = _owned_menu_dimensions()
        addr, size = PROFILES['z8-c311']['tables']['camera.menu.navigation']
        records = size // 32
        self.assertEqual(rows, 16)
        self.assertEqual(handlers, records,
                         'handler slots must match the pinned navigation table')

    def test_pinned_table_record_shape(self):
        # The 12-record table: each record is {u64 event, callback,
        # condition, next}; all records share the next page id 0x68c and
        # route through the 0x403b1b74..0x4054ea5c callback family.
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
        self.assertEqual(len(records), 12)
        self.assertEqual(records[0][0], 0x27e, 'first event must be SETUP (638)')
        callbacks = {r[1] for r in records}
        conditions = {r[2] for r in records}
        self.assertEqual(callbacks, {0x403b1b74, 0x403b1b8c, 0x403b1ba4,
                                     0x403b1ba8, 0x403b1bb0, 0x4054eae4,
                                     0x4054ea5c})
        self.assertEqual(conditions, {0x403b1c5c, 0x403b1c7c})
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

    def test_draw_degradation_is_explicit(self):
        """rectangle/modal_id unpinned: draw_pair must reject and degrade."""
        text = self._source()
        self.assertIn('FxNativeMenuDrawProfile draw={0x403f1940u,0x40325a80u,0x403f0db8u,0u,0x40c7bea4u,0x40325b60u,0u,',
                      text)


if __name__ == '__main__':
    unittest.main()
