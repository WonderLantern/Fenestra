"""Z8 address-space unit test: builds the fixture against the production
fx_z8_space_* code and runs it on an isolated virt machine (no firmware)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import z8_unit_support


class Z8AddressSpaceTests(unittest.TestCase):
    def test_space_contract(self):
        result = z8_unit_support.build_and_run(
            'tests/z8_address_space_fixture.c',
            ['src/platform/z8_c311/address_space.c'],
            'PASS Z8 address_space init-map-pte-duplicate-veto-budget-seal')
        if result is None:
            self.skipTest('clang/ld.lld on PATH and QEMU (FX_QEMU or the '
                          'default z8-qemu build) are required')
        returncode, output, passed = result
        if not passed:
            self.fail(f'fixture failed (rc={returncode}): {output[-400:]}')


if __name__ == '__main__':
    unittest.main()
