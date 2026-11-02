"""Exercise the actual Zf controller with deterministic native task edges."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class ModuleControllerTests(unittest.TestCase):
    def test_multiple_workers_disabled_entries_and_failure_shutdown(self):
        cc=shutil.which('cc') or shutil.which('gcc')
        if not cc:self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'controller'
            subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                '-DFX_ZF_MODULE_WORKERS=1','-I',str(ROOT/'include'),
                '-I',str(ROOT/'src/platform/zf_c301'),
                str(ROOT/'src/platform/zf_c301/module_controller.c'),
                str(ROOT/'src/memory.c'),
                str(ROOT/'tests/zf_module_controller_fixture.c'),'-o',str(binary)],
                check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=15)

if __name__=='__main__':unittest.main()
