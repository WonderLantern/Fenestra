"""Z8 C3.11 container codec tests (clean-room verification).

Uses the local original package and K8 material when available; skips otherwise.
No firmware or key material is committed.
"""
import binascii
import hashlib
import json
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import z8_update_codec as z8

FIRMWARE = Path(os.environ.get('Z8_FW', ''))
SIG = Path(os.environ.get('Z8_K8', ''))
EG_SHA = 'f85e0138e2f5e660ece8560de62e0d12cb795e0e14983e50f20b61dce906c46c'
EG_BODY_START = 581844


def k8():
    try:
        return bytes.fromhex(json.loads(SIG.read_bytes())['K8'])
    except Exception:
        h = os.environ.get('Z8_K8_HEX', '')
        return bytes.fromhex(h) if h else None


@unittest.skipUnless(FIRMWARE.is_file() and k8(), 'local Z8 package / K8 material not present')
class Z8ContainerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = FIRMWARE.read_bytes()
        cls.key = k8()
        cls.tables, cls.body, cls.main, cls.name = z8.decode(cls.raw, cls.key)

    def test_decode_identity(self):
        self.assertEqual(self.name.rstrip(b'\0'), b'eg2070_0311b0.bi')
        self.assertEqual(hashlib.sha256(self.main).hexdigest(), EG_SHA)
        self.assertEqual(len(self.body), len(self.raw) - 32)

    def test_roundtrip_bit_identical(self):
        self.assertEqual(z8.encode(self.body, self.tables, self.key), self.raw)

    def test_single_byte_rebuild_passes_full_structure(self):
        at = EG_BODY_START + 0x1000
        body = bytearray(self.body)
        body[at] ^= 0xFF
        # Repair the module CRC (trailing 2 bytes, big-endian CRC16) and the package CRC
        m_end = EG_BODY_START + len(self.main)
        body[m_end - 2:m_end] = binascii.crc_hqx(bytes(body[EG_BODY_START:m_end - 2]), 0).to_bytes(2, 'big')
        body[-16:-14] = binascii.crc_hqx(bytes(body[:-16]), 0).to_bytes(2, 'big')
        candidate = z8.encode(bytes(body), self.tables, self.key)
        self.assertNotEqual(candidate, self.raw)
        tables, plain, main, name = z8.decode(candidate, self.key, strict=False)
        self.assertNotEqual(main[at - EG_BODY_START], self.main[at - EG_BODY_START])
        self.assertEqual(name.rstrip(b'\0'), b'eg2070_0311b0.bi')
        # Module CRC residue is zero after the repair
        self.assertEqual(binascii.crc_hqx(main, 0), 0)

    def test_rejects_wrong_length_and_digest(self):
        with self.assertRaises(ValueError):
            z8.decode(self.raw[:-1], self.key)
        with self.assertRaises(ValueError):
            z8.decode(self.raw, b'\0' * 8)

    def test_replacement_enforced(self):
        with self.assertRaises(ValueError):
            z8.replace_main(self.body, self.tables, 'eg2070_0400a0.bi', self.main[:-1])
        bad = bytearray(self.main)
        bad[0] ^= 1
        with self.assertRaises(ValueError):
            z8.replace_main(self.body, self.tables, 'eg2070_0400a0.bi', bytes(bad))


if __name__ == '__main__':
    unittest.main()
