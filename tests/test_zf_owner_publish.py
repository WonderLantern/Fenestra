"""Concurrent publisher ownership and native-prefix preservation checks."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class OwnerPublishTests(unittest.TestCase):
    def test_only_one_owner_is_published_and_only_loser_is_freed(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'publish-test'
            args=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-pthread',
                  '-DFX_ZF_EXPERIMENTAL_OWNER_STORAGE=1','-DFX_ZF_EXPERIMENTAL_LIFECYCLE=1',
                  '-I',str(ROOT/'include'),'-I',str(ROOT/'src/platform/zf_c301'),
                  *[str(ROOT/'src/platform/zf_c301'/n) for n in ['owner_storage.c','owner_publish.c','lifecycle.c','ptp.c']],
                  str(ROOT/'src/ptp.c'),str(ROOT/'src/memory.c'),str(ROOT/'tests/zf_owner_publish_fixture.c'),'-o',str(binary)]
            failed=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(failed.returncode,0)
            self.assertIn('experimental',failed.stderr)
            subprocess.run([args[0],'-DFX_ZF_EXPERIMENTAL_OWNER_PUBLISH=1',*args[1:]],check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=15)
if __name__=='__main__':unittest.main()
