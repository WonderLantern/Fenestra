import shutil
import subprocess
import tempfile
import unittest
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform.startswith("linux"), "fixture requires Linux MAP_32BIT")
class ZfAddressSpaceTests(unittest.TestCase):
    def test_owner_probe_and_permissions(self):
        cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:
            self.skipTest('host compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'zf-address-space-test'
            command = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion',
                       '-DFX_ZF_EXPERIMENTAL_ADDRESS_SPACE=1',
                       '-I', str(ROOT / 'include'), '-I', str(ROOT / 'src/platform/zf_c301'),
                       str(ROOT / 'src/platform/zf_c301/address_space.c'),
                       str(ROOT / 'src/memory.c'), str(ROOT / 'tests/zf_address_space_fixture.c'),
                       '-o', str(binary)]
            subprocess.run(command, check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
