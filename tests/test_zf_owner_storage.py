"""Validate unpublished descriptor-compatible Zf owner storage."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class OwnerStorageTests(unittest.TestCase):
    def test_native_prefix_and_sources_are_preserved(self):
        cc=shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        if not cc:self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'owner-test'
            args=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                  '-I',str(ROOT/'include'),'-I',str(ROOT/'src/platform/zf_c301'),
                  str(ROOT/'src/platform/zf_c301/owner_storage.c'),str(ROOT/'src/memory.c'),
                  str(ROOT/'tests/zf_owner_storage_fixture.c'),'-o',str(binary)]
            failed=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(failed.returncode,0)
            subprocess.run([args[0],'-DFX_ZF_EXPERIMENTAL_OWNER_STORAGE=1',*args[1:]],check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=10)
if __name__=='__main__':unittest.main()
