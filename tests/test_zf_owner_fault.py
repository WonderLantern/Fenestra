"""Retain first module identity/error and terminally close owner admission."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class OwnerFaultTests(unittest.TestCase):
    def test_first_fault_and_stop_are_preserved(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('host compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'fault-test'
            command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                '-DFX_ZF_EXPERIMENTAL_OWNER_FAULT=1','-DFX_ZF_EXPERIMENTAL_LIFECYCLE=1',
                '-I',str(ROOT/'include'),'-I',str(ROOT/'src/platform/zf_c301'),
                *[str(ROOT/'src/platform/zf_c301'/n) for n in ['owner_fault.c','lifecycle.c']],
                str(ROOT/'tests/zf_owner_fault_fixture.c'),'-o',str(binary)]
            subprocess.run(command,check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=10)
if __name__=='__main__':unittest.main()
