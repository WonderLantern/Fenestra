"""Host checks for the disabled Zf task adapter; no camera or native CPU."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class ZfTaskTests(unittest.TestCase):
    def test_explicit_guard_and_retained_failures(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:
            self.skipTest('C compiler is required for the host fixture')
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'task-test.exe'
            common=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),
                    '-I',str(ROOT/'src/platform/zf_c301'),str(ROOT/'src/platform/zf_c301/task.c'),
                    str(ROOT/'tests/zf_c301_task_fixture.c'),'-o',str(output)]
            guarded=subprocess.run(common,capture_output=True,text=True)
            self.assertNotEqual(guarded.returncode,0)
            self.assertIn('experimental',guarded.stderr.lower())
            disabled=subprocess.run([*common[:-2],'-DFX_ZF_C301_EXPERIMENTAL_TASK=0',*common[-2:]],
                                    capture_output=True,text=True)
            self.assertNotEqual(disabled.returncode,0)
            self.assertIn('experimental',disabled.stderr.lower())
            subprocess.run([*common[:-2],'-DFX_ZF_C301_EXPERIMENTAL_TASK=1',*common[-2:]],check=True,timeout=30)
            subprocess.run([str(output)],check=True,timeout=10)


if __name__=='__main__':
    unittest.main()
