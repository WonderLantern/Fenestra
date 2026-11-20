import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
from z63_placement import check, constants, guards, position_independent, MARKER


def movz(rd, value, shift=0, wide=1):
    return (wide << 31) | 0x52800000 | ((shift // 16) << 21) | (value << 5) | rd


def movk(rd, value, shift, wide=1):
    return (wide << 31) | 0x72800000 | ((shift // 16) << 21) | (value << 5) | rd


def build64(rd, value):
    return [movz(rd, value & 0xffff)] + [movk(rd, (value >> s) & 0xffff, s) for s in (16, 32, 48)]


class Z63PlacementTests(unittest.TestCase):
    """Synthetic image: one relocated window with an uncopied zero tail."""
    def setUp(self):
        self.base = 0x40000000
        words = build64(21, 0x40002000) + build64(8, 0x40002800) + [movz(23, 0x1, 16, wide=0)]
        routine = struct.pack('<%dI' % len(words), *words)
        image = bytearray(0x20000)
        image[0x100:0x100 + len(routine)] = routine
        image[0x2000:0x2400] = b'\x55' * 0x400
        self.image = bytes(image)
        self.profile = dict(image_base=self.base,
            probe_code_region=dict(begin=0x40001000, end=0x40002000),
            guard_region=dict(begin=0x40002400, end=0x40002800),
            text_relocation=dict(
                routines=[dict(address=self.base + 0x100, bytes=len(routine),
                               sha256=hashlib.sha256(routine).hexdigest())],
                windows=[dict(name='W1', begin=0x40002000, copy_end=0x40002800, remap_end=0x40012000)],
                in_place_executable=dict(begin=0x40001000, end=0x40002000)))

    def test_decodes_built_constants(self):
        words = build64(1, 0x4090ebe8) + [0x2a0103e2 | (1 << 31)]
        self.assertIn(0x4090ebe8, constants(struct.pack('<%dI' % len(words), *words)))

    def test_accepts_in_place_padding(self):
        result = check(self.profile, self.image)
        self.assertTrue(result['outside_relocated_windows'])
        self.assertFalse(result['physical_install_qualified'])

    def test_rejects_zero_tail_of_relocated_window(self):
        # All zero in the image, yet replaced by unwritten DRAM after the remap.
        self.profile['probe_code_region'] = dict(begin=0x40003000, end=0x40004000)
        self.assertFalse(any(self.image[0x3000:0x4000]))
        with self.assertRaisesRegex(ValueError, 'relocated text window W1'):
            check(self.profile, self.image)

    def test_rejects_profile_without_trace(self):
        del self.profile['text_relocation']
        with self.assertRaisesRegex(ValueError, 'unproven'):
            check(self.profile, self.image)

    def test_rejects_bounds_the_original_does_not_build(self):
        self.profile['text_relocation']['windows'][0]['copy_end'] = 0x40002900
        with self.assertRaisesRegex(ValueError, 'not in the original routine'):
            check(self.profile, self.image)

    def test_rejects_changed_routine(self):
        image = bytearray(self.image); image[0x100] ^= 1
        with self.assertRaisesRegex(ValueError, 'routine differs'):
            check(self.profile, bytes(image))

    def test_rejects_region_outside_in_place_range(self):
        self.profile['probe_code_region'] = dict(begin=0x40000800, end=0x40001000)
        with self.assertRaisesRegex(ValueError, 'in-place executable'):
            check(self.profile, self.image)

    def test_rejects_guard_outside_copied_text(self):
        # Zero and executable, but past the copy end: it would vanish with the core.
        self.profile['guard_region'] = dict(begin=0x40003000, end=0x40003400)
        with self.assertRaisesRegex(ValueError, 'not inside copied original text'):
            check(self.profile, self.image)

    def test_rejects_missing_guard_region(self):
        del self.profile['guard_region']
        with self.assertRaisesRegex(ValueError, 'unguarded'):
            check(self.profile, self.image)

    def test_guard_layout_and_fallbacks(self):
        image = bytearray(self.image)
        entry, call = 0x40000400, 0x40000500
        struct.pack_into('<I', image, 0x400, 0xa9bf7bfd)           # stp x29,x30,[sp,#-16]!
        struct.pack_into('<I', image, 0x500, 0x94000000 | 0x40)    # bl +0x100
        code, table = guards(self.profile, bytes(image), {entry: (0x40001010, False), call: (0x40001020, True)})
        self.assertEqual(code[:8], MARKER)
        self.assertEqual(table, {entry: 0x40002408, call: 0x40002428})
        first = struct.unpack_from('<8I', code, 8)
        second = struct.unpack_from('<8I', code, 40)
        self.assertEqual(first[3:5], (0xeb11021f, 0x54000041))
        # Present: both guards enter the core. Absent: entry replays its
        # instruction and continues; the call reaches the original callee.
        self.assertEqual(first[5], 0x14000000 | (((0x40001010 - (table[entry] + 20)) // 4) & 0x3ffffff))
        self.assertEqual(first[6], 0xa9bf7bfd)
        self.assertEqual(first[7], 0x14000000 | (((entry + 4 - (table[entry] + 28)) // 4) & 0x3ffffff))
        self.assertEqual(second[6], 0x14000000 | (((call + 0x100 - (table[call] + 24)) // 4) & 0x3ffffff))

    def test_guard_rejects_pc_relative_entry(self):
        image = bytearray(self.image)
        struct.pack_into('<I', image, 0x400, 0x90000010)           # adrp x16
        self.assertFalse(position_independent(0x90000010))
        with self.assertRaisesRegex(ValueError, 'cannot be displaced'):
            guards(self.profile, bytes(image), {0x40000400: (0x40001010, False)})

    def test_rejects_occupied_region(self):
        image = bytearray(self.image); image[0x1800] = 1
        with self.assertRaisesRegex(ValueError, 'not empty'):
            check(self.profile, bytes(image))


if __name__ == '__main__':
    unittest.main()
