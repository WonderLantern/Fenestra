"""Compile and run the shared locale-bank C regression fixture on the host."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class NativeFontLanguageTests(unittest.TestCase):
    def test_dynamic_locale_banks(self):
        local=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang.exe')
        compiler=str(local) if local.is_file() else shutil.which('clang') or shutil.which('gcc')
        if not compiler:raise RuntimeError('Clang or GCC required for real shared C text-bank tests')
        base=ROOT/'build';base.mkdir(exist_ok=True);folder=Path(tempfile.mkdtemp(prefix='font-language-',dir=base))
        binary=folder/'font-language-test.exe'
        command=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-DFX_Z8_C311_FULL=1','-I','include',
            'src/native_font.c','src/memory.c','src/text_regions.c','tests/test_native_font_languages.c','-o',str(binary)]
        built=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
        (folder/'result.json').write_text(json.dumps({'command':command,'returncode':result.returncode,'stdout':result.stdout,
            'stderr':result.stderr,'physical_hardware_verified':False},indent=2)+'\n')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main(verbosity=2)
