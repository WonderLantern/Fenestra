"""Compile and run the real storage/journal code with explicit filesystem fixtures."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class Z8StorageTests(unittest.TestCase):
    def test_compiled_volume_selection_and_journal_ownership(self):
        compiler=shutil.which('clang') or shutil.which('gcc')
        if not compiler:
            candidate=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang.exe')
            if candidate.is_file():compiler=str(candidate)
        if not compiler:raise RuntimeError('Clang or GCC required for compiled storage fixture')
        with tempfile.TemporaryDirectory(prefix='fenestra-z8-storage-') as directory:
            output=Path(directory)/('storage.exe' if os.name=='nt' else 'storage')
            command=[compiler,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),
                '-I'+str(ROOT/'src/platform/z8_c311_full'),str(ROOT/'tests/test_z8_storage.c'),
                str(ROOT/'src/platform/z8_c311_full/storage.c'),str(ROOT/'src/startup.c'),str(ROOT/'src/memory.c'),'-o',str(output)]
            built=subprocess.run(command,capture_output=True,text=True,timeout=45)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(output)],check=True,capture_output=True,text=True,timeout=10)
            self.assertIn('PASS: SD and CF selection',result.stdout)
if __name__=='__main__':unittest.main()
