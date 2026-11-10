"""Compile the actual Z8 read adapter with a noncoherent DMA/cache fixture."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class Z8CachedReadTests(unittest.TestCase):
    def test_noncoherent_dma_and_cleanup(self):
        cc=shutil.which('clang') or shutil.which('gcc')
        if not cc:
            p=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang.exe')
            if p.is_file():cc=str(p)
        if not cc:raise RuntimeError('Clang or GCC required')
        with tempfile.TemporaryDirectory(prefix='fenestra-z8-read-') as d:
            d=Path(d)
            (d/'z8_full_bindings.h').write_text(
                '#include <stdint.h>\n'
                'void *fixture_allocate(uint32_t,uint32_t);\n'
                'void fixture_release(void *);\n'
                'uint32_t fixture_read(int32_t,void *,uint32_t,uint32_t *);\n'
                '#define FX_Z8_ADDR_40d30458 ((uintptr_t)&fixture_allocate)\n'
                '#define FX_Z8_ADDR_40d30498 ((uintptr_t)&fixture_release)\n'
                '#define FX_Z8_ADDR_402dcb30 ((uintptr_t)&fixture_read)\n')
            exe=d/('probe.exe' if os.name=='nt' else 'probe')
            result=subprocess.run([cc,'-std=c11','-O1','-Wall','-Wextra','-Werror',
                '-I'+str(d),'-I'+str(ROOT/'include'),str(ROOT/'tests/test_z8_cached_read.c'),
                str(ROOT/'src/platform/z8_c311_full/file_read.c'),str(ROOT/'src/memory.c'),'-o',str(exe)],
                capture_output=True,text=True,timeout=45)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS: dirty-cache DMA regression',result.stdout)
if __name__=='__main__':unittest.main()
