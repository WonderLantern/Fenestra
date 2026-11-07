"""Z8 C3.11 task-family pinning tests: 16-byte entry-window sha256 anchors
against the local C311 MAIN image (Z8_MAIN; auto-skip when absent)."""
import hashlib
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
from platform_profiles import PROFILES

Z8_MAIN = Path(os.environ.get('Z8_MAIN', ''))
BASE = 0x40160000
C311_MAIN_SHA256 = 'f85e0138e2f5e660ece8560de62e0d12cb795e0e14983e50f20b61dce906c46c'
# (address, 16-byte entry-window sha256, window bytes) pinned from the C311
# image. The numbers equal the former Z9 references; each pin is derived from
# C311 bytes alone (docs/validation-z8-c311.md task row, method notes in the
# parent-repo t12 engineering record, section B1).
# create FUN_40185064: 56-byte record validation, TCB pop from the free list
# 0xa4b7568, ready-list insertion, returns slot+1 (TCB stride 0x78).
# resume FUN_401855e0: id bounds vs the count at 0xa2aaab8, wake via the id
# table 0xa499e430, scheduler kick 0x40189828.
# delay FUN_40187b00: current-TCB wake-list link, wake tick = now + ticks,
# block + scheduler kick.
TASK_PINS = {
    'rtos.task.create': (
        0x40185064,
        '779f945a18359468198e50bc455e267dc7b95a38ca438b2b5a4b958c067dda1a',
        'ffc302d1fc6f05a9fa6706a9f85f07a9'),
    'rtos.task.resume': (
        0x401855e0,
        '2cf1702ed32e92dc76bb8d294ac198d1c292f0609a5c74a59e0c589e95405106',
        'f553bea9f37b01a928423bd5087d4092'),
    'rtos.task.delay': (
        0x40187b00,
        'ed1e5ffea9164db1ea314fd29f40c97e3fbce8291fec64c59892ff1e7af80995',
        'f75bbda9f55301a9f37b02a928423bd5'),
}


class Z8TaskPinOfflineTests(unittest.TestCase):
    def test_profile_functions_match_pins(self):
        functions = PROFILES['z8-c311']['functions']
        for name, (address, _, _) in TASK_PINS.items():
            self.assertEqual(functions[name], address, name)

    def test_sources_carry_pinned_addresses(self):
        rtos = (ROOT / 'src/platform/z8_c311/rtos.c').read_text(encoding='utf-8')
        task = (ROOT / 'src/platform/z8_c311/task.c').read_text(encoding='utf-8')
        for address, _, _ in TASK_PINS.values():
            literal = f'UINT64_C({hex(address)})'
            self.assertIn(literal, rtos, literal)
            self.assertIn(literal, task, literal)


@unittest.skipUnless(Z8_MAIN.is_file(), 'local Z8 MAIN image (Z8_MAIN) not present')
class Z8TaskPinImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = Z8_MAIN.read_bytes()

    def test_image_is_the_pinned_c311_main(self):
        self.assertEqual(hashlib.sha256(self.image).hexdigest(), C311_MAIN_SHA256)

    def test_entry_windows(self):
        for name, (address, digest, window) in TASK_PINS.items():
            data = self.image[address - BASE:address - BASE + 16]
            self.assertEqual(len(data), 16, name)
            self.assertEqual(data.hex(), window, name)
            self.assertEqual(hashlib.sha256(data).hexdigest(), digest, name)


if __name__ == '__main__':
    unittest.main()
