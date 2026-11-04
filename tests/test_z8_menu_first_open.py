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
        for event, callback, condition, nxt in records:
            self.assertTrue(0x40160000 <= callback < 0x45000000)
            self.assertTrue(0x40160000 <= condition < 0x45000000)
            self.assertEqual(nxt, 0x68c)

    def test_no_z9_navigation_constants_remain(self):
        sources = (ROOT / 'src' / 'platform' / 'z8_c311').glob('*.c')
        for source in list(sources) + [MENU_DATA]:
            text = source.read_text()
            self.assertNotRegex(text, r'UINT64_C\(0x43014bb8\)', source.name)


if __name__ == '__main__':
    unittest.main()
