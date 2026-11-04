"""Offline Z8 container-codec tests: synthetic inputs only, no firmware material."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import z8_update_codec as z8


def single_table(active, index):
    """Tables with exactly one nonzero byte in one of t1/t2/t3."""
    zero = bytes(256)
    one = bytearray(256)
    one[index] = 0x5A
    return {'t1': (bytes(one), zero, zero), 't2': (zero, bytes(one), zero),
            't3': (zero, zero, bytes(one))}[active]


class Z8CodecOfflineTests(unittest.TestCase):
    def test_xor_stream_is_own_inverse(self):
        body = (b'L' * 32) + bytes(range(256)) * 16 + b'\0' * 32
        tables = (bytes(range(256)), bytes(reversed(range(256))), bytes([7]) * 256)
        cipher = z8.xor_stream(body, tables)
        self.assertTrue(cipher.startswith(b'L' * 32))  # the 32-byte label stays clear
        self.assertEqual(z8.xor_unstream(cipher, tables), body)

    def test_stream_index_selects_tables_by_body_offset(self):
        # One table, one nonzero byte: the set of positions where the cipher
        # differs from the plain body is exactly that table's selection rule.
        length = 200032  # 32-byte label + 200000 body; covers t1/t2/t3 index transitions
        plain = bytes(length)
        cases = {
            't1': 5,    # i % 256 == 5
            't2': 1,    # (i >> 8) % 256 == 1
            't3': 1,    # (i >> 16) % 256 == 1 -> [65536, 131072)
        }
        for active, index in cases.items():
            cipher = z8.xor_stream(plain, single_table(active, index))
            diff = {i for i in range(32, length) if cipher[i] != plain[i]}
            if active == 't1':
                want = {i for i in range(32, length) if i % 256 == index}
            elif active == 't2':
                want = {i for i in range(32, length) if (i >> 8) % 256 == index}
            else:
                want = {i for i in range(32, length) if (i >> 16) % 256 == index}
            self.assertEqual(diff, want, active)

    def test_decode_rejects_synthetic_inputs_before_key_use(self):
        # Four rejection paths ahead of K8 authentication, all reachable offline.
        with self.assertRaisesRegex(ValueError, 'unexpected package size'):
            z8.decode(bytes(32), b'\0' * 20)
        raw = bytearray(z8.RAW_BYTES)  # ~99 MB all-zero synthetic package
        with self.assertRaisesRegex(ValueError, 'outer field differs'):
            z8.decode(raw, b'\0' * 20, strict=False)  # skip the SHA pin; outer field fires
        raw[20:32] = z8.OUTER_FIELD
        with self.assertRaisesRegex(ValueError, 'original package SHA-256 mismatch'):
            z8.decode(raw, b'\0' * 20)                 # default strict hits the SHA pin first
        with self.assertRaisesRegex(ValueError, 'plaintext label mismatch'):
            z8.decode(raw, b'\0' * 20, strict=False)  # zero tables decode a zero body


if __name__ == '__main__':
    unittest.main()
