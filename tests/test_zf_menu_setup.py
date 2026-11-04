"""Exercise the unpublished Zf Setup descriptor construction."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ZfSetupTests(unittest.TestCase):
    def test_copy_preserves_stock_and_rejects_bad_inputs(self):
        compiler = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        if not compiler:
            self.skipTest('host C compiler is unavailable')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'zf-menu-setup-test.exe'
            platform = ROOT / 'src/platform/zf_c301'
            rejected = subprocess.run([compiler, '-std=c11', '-DFX_ZF_EXPERIMENTAL_MENU_SETUP=0',
                                       '-I', str(ROOT / 'include'), '-I', str(platform),
                                       '-c', str(platform / 'menu_setup.c'), '-o', str(output)], capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(output.exists())
            command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
                       '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                       '-DFX_ZF_EXPERIMENTAL_MENU_SETUP', '-I', str(ROOT / 'include'), '-I', str(platform),
                       str(platform / 'menu_setup.c'), str(ROOT / 'src/native_menu.c'),
                       str(ROOT / 'src/memory.c'), str(ROOT / 'tests/test_zf_menu_setup.c'), '-o', str(output)]
            subprocess.run(command, check=True, timeout=30)
            subprocess.run([str(output)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
