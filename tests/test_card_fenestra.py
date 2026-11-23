"""Validate the Core-to-Port ABI without original firmware or a camera."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CorePortTests(unittest.TestCase):
    def test_port_header_identity_and_bounds(self):
        compiler = shutil.which('clang') or shutil.which('gcc')
        self.assertIsNotNone(compiler,'Clang or GCC is required for the Port ABI fixture')
        build = ROOT/'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='core-port-',dir=build) as temporary:
            binary = Path(temporary)/'fixture.exe'
            command = [compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                       '-I','include','src/core_port.c','src/memory.c','tests/test_core_port.c','-o',str(binary)]
            built = subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=120)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result = subprocess.run([str(binary)],cwd=ROOT,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':
    unittest.main()
