"""Synthetic checks for the unintegrated Zf core placement planner."""
import binascii
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import zf_c301_core_plan as core


class ZfCorePlanTests(unittest.TestCase):
    def test_plan_rechecks_original_words_and_marks_unqualified(self):
        payload = bytearray(max(core.COPY_ENDPOINT - core.BASE + 8,
                                core.HOOK_SITE - core.BASE + 4,
                                core.CAVE_VA - core.BASE + core.CAVE_BYTES))
        payload[core.COPY_ENDPOINT-core.BASE:core.COPY_ENDPOINT-core.BASE+8] = b'\x08\xde\x87\xd2\xa8\x11\xa8\xf2'
        payload[core.HOOK_SITE-core.BASE:core.HOOK_SITE-core.BASE+4] = core.HOOK_ORIGINAL.to_bytes(4,'little')
        payload.extend(b'\0' * (core.EG_SIZE - 2 - len(payload)))
        digest = __import__('hashlib').sha256(payload).hexdigest()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            app = root / 'app'; app.write_bytes(payload)
            out = core.ROOT / 'build' / ('zf-core-test-' + next(tempfile._get_candidate_names()))
            with patch.multiple(core, EG_SIZE=len(payload)+2, EG_PAYLOAD_SHA256=digest):
                result = core.plan(app, out)
            self.assertFalse(result['runtime_build_available'])
            self.assertFalse(result['hook_installed'])
            self.assertEqual(int(result['hook']['branch_word'], 16), core.HOOK_BRANCH)
            self.assertTrue(result['copy_endpoint']['required'])
            self.assertFalse(result['firmware_output_generated'])
            self.assertEqual(json.loads((out/'core-plan.json').read_text())['placement']['bytes'], core.CAVE_BYTES)
            out2 = out.parent / (out.name + '-bad')
            bad = bytearray(payload); bad[core.HOOK_SITE-core.BASE] ^= 1
            app.write_bytes(bad)
            with patch.multiple(core, EG_SIZE=len(payload)+2, EG_PAYLOAD_SHA256=digest):
                with self.assertRaises(ValueError): core.plan(app, out2)
            import shutil; shutil.rmtree(out)

    def test_mapped_region_is_explicit_and_larger_than_the_cave(self):
        image = bytearray(core.EG_SIZE - 2)
        image[core.COPY_ENDPOINT-core.BASE:core.COPY_ENDPOINT-core.BASE+8] = b'\x08\xde\x87\xd2\xa8\x11\xa8\xf2'
        image[core.HOOK_SITE-core.BASE:core.HOOK_SITE-core.BASE+4] = core.HOOK_ORIGINAL.to_bytes(4,'little')
        digest = __import__('hashlib').sha256(image).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'app'; app.write_bytes(image)
            out = core.ROOT / 'build' / ('zf-core-region-' + next(tempfile._get_candidate_names()))
            with patch.multiple(core, EG_PAYLOAD_SHA256=digest):
                result = core.plan(app, out, placement='mapped-region')
            self.assertEqual(result['placement']['bytes'], 0x60000)
            self.assertTrue(result['placement']['qemu_mapping_verified'])
            word = int(result['hook']['branch_word'], 16)
            immediate = word & 0x03ffffff
            if immediate & (1 << 25): immediate -= 1 << 26
            self.assertEqual(word >> 26, 0b000101)
            self.assertEqual(core.HOOK_SITE + (immediate << 2), core.MAPPED_CODE_VA)
            self.assertEqual(word, 0x17c16087)
            self.assertFalse(result['copy_endpoint']['required'])
            import shutil; shutil.rmtree(out)

    def test_bad_crc_is_rejected_for_complete_module(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'bad'
            path.write_bytes(b'not an EG')
            with self.assertRaises(ValueError): core.plan(path, core.ROOT/'build'/'zf-core-bad-test')


if __name__ == '__main__': unittest.main()
