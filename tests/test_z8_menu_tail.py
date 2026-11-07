"""Z8 routed-page descriptor tail contracts (T12-①).

The C311 open-path staged visible-row builder (FUN_403b01f0, reached
through the stock callbacks on every routed FxNativeUi-family page's
first open) loads its visible-row vector from *(descriptor+0x40): the
stock descriptor is 0x50 bytes (ROM template 0x44d35040 stride 0x50
carries a real record-table pointer at +0x40; the SETUP construct
already copies that tail verbatim). Shared-built routed pages used to
leave the copied header bytes at +0x40, so the builder dereferenced
header words {0,6} as a pointer (x23=0x600000000) and stopped the
machine on the unmapped read (t5-manager-reid §3.1). Source contracts
plus a virt-machine fixture that exercises the shared builder.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'host'))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from z8_unit_support import build_and_run  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENU_C = ROOT / 'src' / 'platform' / 'z8_c311' / 'menu.c'
NATIVE_MENU_C = ROOT / 'src' / 'native_menu.c'
NATIVE_MENU_H = ROOT / 'include' / 'fenestra_native_menu.h'


class Z8DescriptorTailSources(unittest.TestCase):
    def test_shared_builder_publishes_profile_tail(self):
        text = NATIVE_MENU_C.read_text()
        self.assertIn('out->descriptor_tail[0] = p->descriptor_tail ? p->descriptor_tail[0] : 0;',
                      text)
        self.assertIn('out->descriptor_tail[1] = p->descriptor_tail ? p->descriptor_tail[1] : 0;',
                      text)

    def test_tail_field_follows_descriptor(self):
        text = NATIVE_MENU_H.read_text()
        self.assertIn('uint64_t descriptor_tail[2];', text)
        self.assertIn('const uint64_t *descriptor_tail;', text)

    def test_routed_profiles_carry_the_empty_staging_vector(self):
        """The three routed profile literals (module UI, apps, manager)
        must publish the empty staging vector; own-page profiles keep the
        zero default (no fault was ever observed there)."""
        text = MENU_C.read_text()
        self.assertIn('static const uint64_t fx_z8_descriptor_tail[2]', text)
        self.assertEqual(text.count('fx_z8_descriptor_tail'), 4)
        self.assertEqual(text.count('13u,fx_z8_descriptor_tail}'), 3)


class Z8DescriptorTailFixture(unittest.TestCase):
    """Runs the shared builder in the isolated virt machine."""

    def test_builder_publishes_descriptor_tail(self):
        result = build_and_run(
            'tests/z8_menu_tail_fixture.c',
            ['src/native_menu.c'],
            'TAIL OK', ld='tests/z8_unit_fixture.ld')
        if result is None:
            self.skipTest('toolchain or emulator unavailable')
        rc, output, passed = result
        self.assertEqual(rc, 0, output)
        self.assertTrue(passed, output)


if __name__ == '__main__':
    unittest.main()
