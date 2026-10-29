"""Host protocol tests; these do not represent physical USB qualification."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]


class PtpTests(unittest.TestCase):
    def test_protocol(self):
        base=ROOT/'build';base.mkdir(exist_ok=True)
        folder=Path(tempfile.mkdtemp(prefix='ptp-protocol-',dir=base))
        binary=folder/'ptp-test.exe'
        compiler=shutil.which('clang') or shutil.which('gcc')
        if not compiler:raise RuntimeError('Clang or GCC is required for the real protocol fixture')
        command=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wconversion',
                 '-Wshadow','-I','include','src/ptp.c','src/memory.c','tests/test_ptp.c','-o',str(binary)]
        built=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        ran=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
        (folder/'result.json').write_text(json.dumps(dict(command=command,returncode=ran.returncode,
            stdout=ran.stdout,stderr=ran.stderr,physical_usb_verified=False),indent=2))
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
        print(ran.stdout.strip())


if __name__=='__main__':unittest.main()
