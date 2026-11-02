"""Static-only Zf UI dispatch candidate preparation tests."""
import binascii
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
import zf_c301_ui_dispatch as ui


class ZfUiDispatchTests(unittest.TestCase):
    def test_prepares_bounded_candidates_without_runtime_claim(self):
        payload = bytes((i * 17) & 0xff for i in range(4096))
        image = payload + binascii.crc_hqx(payload, 0).to_bytes(2, 'big')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / 'application'
            original.write_bytes(image)
            candidates = (
                dict(ui.UI_CANDIDATES[0], start=ui.BASE, end=ui.BASE + 32,
                     sha256=hashlib.sha256(image[:32]).hexdigest()),
                dict(ui.UI_CANDIDATES[1], start=ui.BASE + 64, end=ui.BASE + 96,
                     sha256=hashlib.sha256(image[64:96]).hexdigest()),
            )
            with patch.multiple(ui, ROOT=root, EG_SIZE=len(image), EG_SHA256=hashlib.sha256(image).hexdigest(),
                                EG_PAYLOAD_SHA256=hashlib.sha256(payload).hexdigest(),
                                UI_CANDIDATES=candidates):
                result = ui.prepare(original, root / 'build' / 'candidates')
            self.assertEqual(result['status'], 'STATIC_CANDIDATES_ONLY')
            self.assertFalse(result['runtime_qualified'])
            self.assertFalse(result['installation_hook_generated'])
            self.assertEqual([row['bytes'] for row in result['candidates']], [32, 32])
            self.assertEqual((root / 'build/candidates/fx_zf_ui_candidate_a.inc').read_text().splitlines()[0].strip(),
                             '0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,')

    def test_rejects_changed_window_and_reuse(self):
        payload = bytes(range(256))
        image = payload + binascii.crc_hqx(payload, 0).to_bytes(2, 'big')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / 'application'
            original.write_bytes(image)
            candidate = dict(ui.UI_CANDIDATES[0], start=ui.BASE, end=ui.BASE + 16,
                             sha256=hashlib.sha256(image[:16]).hexdigest())
            with patch.multiple(ui, ROOT=root, EG_SIZE=len(image), EG_SHA256=hashlib.sha256(image).hexdigest(),
                                EG_PAYLOAD_SHA256=hashlib.sha256(payload).hexdigest(),
                                UI_CANDIDATES=(candidate,)):
                output = root / 'build' / 'candidate'
                ui.prepare(original, output)
                with self.assertRaises(ValueError):
                    ui.prepare(original, output)
                original.write_bytes(image[:-1] + bytes([image[-1] ^ 1]))
                with self.assertRaises(ValueError):
                    ui.prepare(original, root / 'build' / 'changed')


if __name__ == '__main__':
    unittest.main()
