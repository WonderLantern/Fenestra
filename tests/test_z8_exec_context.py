"""Z8 exec-context unit test: builds the fixture against the production
private gate and runs it on an isolated virt machine (no firmware)."""
import sys
import unittest

sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parent))
import z8_unit_support


class Z8ExecContextTests(unittest.TestCase):
    def test_gate_contract(self):
        result = z8_unit_support.build_and_run(
            'tests/z8_exec_context_fixture.c',
            ['src/platform/z8_c311/exec_context.c',
             'src/platform/z8_c311/private_call.S',
             'src/platform/z8_c311/user_call.S'],
            'PASS Z8 exec_context check-count fault-injection faults-gate')
        if result is None:
            self.skipTest('clang/ld.lld on PATH and QEMU (FX_QEMU or the '
                          'default z8-qemu build) are required')
        returncode, output, passed = result
        if not passed:
            self.fail(f'fixture failed (rc={returncode}): {output[-400:]}')


if __name__ == '__main__':
    unittest.main()
