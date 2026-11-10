import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
from z63_package import verify_application, xor3, seal_header
from z63_placement import guards, MARKER


class Z63PackageTests(unittest.TestCase):
    def test_digest_sealing_preserves_all_other_header_bytes(self):
        suffix=b'fixture8';body=b'original fixture';replacement=b'changed fixture'
        header=bytearray((i%256 for i in range(608)))
        header[0x220:0x240]=header[:32][::-1]
        header[0x240:0x254]=hashlib.sha1(body+suffix).digest()
        sealed=seal_header(header,body,replacement,suffix)
        self.assertEqual(sealed[:0x240],header[:0x240])
        self.assertEqual(sealed[0x254:],header[0x254:])
        self.assertEqual(sealed[0x240:0x254],hashlib.sha1(replacement+suffix).digest())
        with self.assertRaises(ValueError):seal_header(header,body,replacement,b'wrong888')

    def setUp(self):
        # Offset 64: the hooked site, a position-independent instruction.
        self.original = bytes(range(64)) + struct.pack('<I', 0xa9bf7bfd) + bytes(252 + 256)
        self.profile = dict(image_base=0x4000, application_sha256=hashlib.sha256(self.original).hexdigest(),
            probe_code_region=dict(begin=0x4080, end=0x4100), guard_region=dict(begin=0x4100, end=0x4200),
            sysctl=dict(dispatcher=0x4040), menu_hooks={})
        self.candidate = bytearray(self.original)
        self.candidate[128:132] = b'code'
        self.candidate[0xf8:0x100] = MARKER
        guard, table = guards(self.profile, self.original, {0x4040: (0x4080, False)})
        self.candidate[0x100:0x100 + len(guard)] = guard
        struct.pack_into('<I', self.candidate, 64, 0x14000000 | ((table[0x4040] - 0x4040) // 4))
        self.receipt = dict(target='z63-c201', hooks={'0x4040':'fx_z63_system_dispatch'},
            symbols={'fx_z63_system_dispatch':0x4080})

    def check(self):
        self.receipt['candidate_sha256'] = hashlib.sha256(self.candidate).hexdigest()
        verify_application(self.original, self.candidate, self.receipt, self.profile)

    def test_exact_declared_reconstruction(self):
        self.check()

    def test_rejects_undeclared_boot_change(self):
        self.candidate[0] ^= 1
        with self.assertRaisesRegex(ValueError, 'undeclared application'):
            self.check()

    def test_rejects_wrong_branch_destination(self):
        self.receipt['symbols']['fx_z63_system_dispatch'] += 4
        with self.assertRaisesRegex(ValueError, 'guards differ'):
            self.check()
        self.receipt['symbols']['fx_z63_system_dispatch'] = 0x4200
        with self.assertRaisesRegex(ValueError, 'resident symbol'):
            self.check()

    def test_rejects_hook_that_bypasses_its_guard(self):
        struct.pack_into('<I', self.candidate, 64, 0x14000010)
        with self.assertRaisesRegex(ValueError, 'branch to its guard'):
            self.check()

    def test_rejects_altered_guard(self):
        self.candidate[0x100 + 8 + 16] ^= 1
        with self.assertRaisesRegex(ValueError, 'guards differ'):
            self.check()

    def test_rejects_missing_marker(self):
        self.candidate[0xf8:0x100] = bytes(8)
        with self.assertRaisesRegex(ValueError, 'marker missing'):
            self.check()

    def test_rejects_unknown_hook(self):
        self.receipt['hooks'] = {'0x4044':'fx_z63_system_dispatch'}
        with self.assertRaisesRegex(ValueError, 'undeclared target hook'):
            self.check()

    def test_paired_ctr_mutation_and_round_trip(self):
        # Known independent bytewise result, including an embedded zero.
        self.assertEqual(xor3(b'\x11\x22\x33', b'\x00\xff\x80', b'\x01\xfe\x80'), b'\x10\x23\x33')
        original = bytes(range(256)) * 4097
        encoded = bytes(reversed(range(256))) * 4097
        changed = bytearray(original)
        changed[1048576] ^= 4
        rebuilt = xor3(encoded, original, changed)
        self.assertEqual(xor3(encoded, original, rebuilt), changed)
        with self.assertRaises(ValueError):
            xor3(b'a', b'', b'a')


if __name__ == '__main__':
    unittest.main()
