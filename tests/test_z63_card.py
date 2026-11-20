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
    def test_startup_short_reads_and_uncertain_close(self):
        cc = shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'startup-test'
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wconversion', '-Wshadow', '-I', str(ROOT / 'include'),
                            str(ROOT / 'tests/z63_startup_fixture.c'), str(ROOT / 'src/startup_files.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_manager_short_names_and_uncertain_rename(self):
        cc = shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'manager-test'
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wconversion', '-Wshadow', '-I', str(ROOT / 'include'),
                            str(ROOT / 'tests/z63_manager_fixture.c'), str(ROOT / 'src/memory.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

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

    def test_native_directory_records_and_retention(self):
        cc = shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'directory-test'
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wconversion', '-Wshadow', '-I', str(ROOT / 'include'),
                            str(ROOT / 'tests/z63_discovery_fixture.c'), str(ROOT / 'src/memory.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_lifecycle_and_terminal_stop(self):
        cc = shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'lifecycle-test'
            subprocess.run([cc, '-std=c11', *([] if sys.platform=='win32' else ['-pthread']), '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/z63_lifecycle_fixture.c'),
                            str(ROOT / 'src/platform/z63_c201/lifecycle.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_task_create_resume_and_retention(self):
        cc = shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'task-test'
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wconversion', '-Wshadow', '-I', str(ROOT / 'include'),
                            str(ROOT / 'tests/z63_task_fixture.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_translated_resident_code_mapping(self):
        cc = shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('C compiler unavailable')
        # The fixture stores host pointers in guest page-table entries.
        # Keep its image below the guest's 39-bit address limit. Windows
        # ASLR can place the default image above that limit. These flags
        # affect only this fixture; the production bounds stay intact.
        link_flags = ['-no-pie'] if sys.platform.startswith('linux') else []
        if sys.platform == 'win32':
            link_flags = ['-Wl,/base:0x10000000', '-Wl,/dynamicbase:no']
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'mapping-test'
            subprocess.run([cc, *link_flags, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(ROOT / 'tests/z63_address_space_fixture.c'),
                            str(ROOT / 'src/platform/z63_c201/address_space.c'), str(ROOT / 'src/memory.c'),
                            '-o', str(binary)], check=True)
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
