import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
from build_z63_adapter import verify


class Z63CardTests(unittest.TestCase):
    def test_card_errors_counts_and_retention(self):
        cc = shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'card-test'
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wconversion', '-Wshadow', '-I', str(ROOT / 'include'),
                            str(ROOT / 'tests/z63_card_fixture.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_reject_wrong_application(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'application'
            path.write_bytes(b'invalid')
            with self.assertRaisesRegex(ValueError, 'wrong Z6III'):
                verify(path)

    @unittest.skipUnless(os.environ.get('FENESTRA_Z63_EG'), 'local original C2.01 EG not supplied')
    def test_original_bindings(self):
        self.assertEqual(verify(os.environ['FENESTRA_Z63_EG'])['target'], 'z63-c201')
