"""Regression checks for the Zf card/discovery fixes, without camera inputs."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
import build_zf_link81


class Link81Tests(unittest.TestCase):
    def test_partial_sector_read_status_eof_and_uppercase_discovery(self):
        cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:
            self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'adapters.exe'
            sources = ['src/platform/zf_c301/card_read.c',
                       'src/platform/zf_c301/directory_case.c',
                       'tests/zf_link81_adapters_fixture.c']
            subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), '-I', str(ROOT / 'src/platform/zf_c301'),
                            *[str(ROOT / name) for name in sources], '-o', str(binary)],
                           check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_changed_base_is_rejected_before_patching(self):
        with self.assertRaisesRegex(ValueError, 'base EG differs'):
            build_zf_link81.apply_corrections(b'unrelated EG', b'', 0, b'')


if __name__ == '__main__':
    unittest.main()
