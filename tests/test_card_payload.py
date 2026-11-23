"""Host packer and real C transport tests using synthetic files."""
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'host'))
from card_payload import ADDRESS, CAPACITY, pack


class CardPayloadTests(unittest.TestCase):
    def test_format_and_bounds(self):
        image = bytes.fromhex('1f2003d5c0035fd6')
        self.assertEqual(pack(image,4),struct.pack('<4sIII',b'FNB1',16,8,4)+image)
        for entry in (-4,2,8):
            with self.assertRaises(ValueError):
                pack(image,entry)
        for size in (3,CAPACITY+1):
            with self.assertRaises(ValueError):
                pack(bytes(size),0)
        header = (ROOT/'include/fenestra_card_payload.h').read_text(encoding='ascii')
        self.assertIn(f'FX_CARD_PAYLOAD_ADDRESS {ADDRESS:#x}',header)
        self.assertIn(f'FX_CARD_PAYLOAD_CAPACITY {CAPACITY:#x}',header)

    def test_native_reader_and_packer(self):
        compiler = shutil.which('clang') or shutil.which('gcc')
        if not compiler:
            self.fail('Clang or GCC is required for the card reader fixture')
        build = ROOT/'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='card-payload-',dir=build) as tmp:
            folder = Path(tmp)
            binary = folder/'fixture.exe'
            compiled = subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wconversion',
                '-Wshadow','-I','include','src/card_payload.c','src/memory.c','tests/card_payload_fixture.c',
                '-o',str(binary)],cwd=ROOT,capture_output=True,text=True,timeout=120)
            self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
            fixture = subprocess.run([str(binary)],cwd=ROOT,capture_output=True,text=True,timeout=10)
            self.assertEqual(fixture.returncode,0,fixture.stdout+fixture.stderr)
            raw, card_file = folder/'entry.raw',folder/'BOOT.BIN'
            image = bytes.fromhex('c0035fd6')
            raw.write_bytes(image)
            command = [sys.executable,'-B','fenestra.py','card-payload','--image',str(raw),
                       '--output',str(card_file)]
            wrapped = subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=10)
            self.assertEqual(wrapped.returncode,0,wrapped.stdout+wrapped.stderr)
            self.assertEqual(card_file.read_bytes(),pack(image,0))
            duplicate = subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=10)
            self.assertNotEqual(duplicate.returncode,0)
            self.assertEqual(card_file.read_bytes(),pack(image,0))


if __name__=='__main__':
    unittest.main()
