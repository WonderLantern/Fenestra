"""Native task acceptance versus actual entry and terminal consumption."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class OwnerTaskTests(unittest.TestCase):
    def test_entry_consumes_notices_and_stops_without_restarting(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'worker-test'
            cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                 '-DFX_ZF_EXPERIMENTAL_LIFECYCLE=1','-DFX_ZF_C301_EXPERIMENTAL_TASK=1',
                 '-I',str(ROOT/'include'),'-I',str(ROOT/'src/platform/zf_c301'),
                 *[str(ROOT/'src/platform/zf_c301'/n) for n in ['owner_task.c','task.c','lifecycle.c']],
                 str(ROOT/'tests/zf_owner_task_fixture.c'),'-o',str(binary)]
            rejected=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(rejected.returncode,0)
            subprocess.run([cmd[0],'-DFX_ZF_EXPERIMENTAL_OWNER_TASK=1',*cmd[1:]],check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=15)
if __name__=='__main__':unittest.main()
