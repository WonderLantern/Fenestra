"""Host concurrency and event-order checks for the Zf owner inbox."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class ZfLifecycleTests(unittest.TestCase):
    def test_coalescing_terminal_stop_and_concurrent_callbacks(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'life-test'
            args=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-pthread',
                  '-I',str(ROOT/'src/platform/zf_c301'),
                  str(ROOT/'src/platform/zf_c301/lifecycle.c'),
                  str(ROOT/'tests/zf_lifecycle_fixture.c'),'-o',str(binary)]
            rejected=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(rejected.returncode,0)
            self.assertIn('experimental',rejected.stderr)
            subprocess.run([args[0],'-DFX_ZF_EXPERIMENTAL_LIFECYCLE=1',*args[1:]],check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=15)

if __name__=='__main__':unittest.main()
