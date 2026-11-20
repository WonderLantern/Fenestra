"""Host fixture for card module discovery; no camera or firmware material."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]


class DiscoveryTests(unittest.TestCase):
    def test_invalid_names_are_skipped(self):
        base=ROOT/'build';base.mkdir(exist_ok=True)
        folder=Path(tempfile.mkdtemp(prefix='discovery-',dir=base))
        binary=folder/'discovery-test.exe'
        compiler=shutil.which('clang') or shutil.which('gcc')
        if not compiler:raise RuntimeError('Clang or GCC is required for the discovery fixture')
        command=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-I','include',
                 'src/services.c','src/module.c','src/sha256.c','src/memory.c','tests/test_discovery.c','-o',str(binary)]
        built=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        ran=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
        print(ran.stdout.strip())


if __name__=='__main__':unittest.main()
