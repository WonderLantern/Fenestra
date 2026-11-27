"""Linux VFS integration in an explicitly isolated mount namespace."""
import errno
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform.startswith('linux'), 'Linux syscall helper')
class LinuxFileOverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / 'build'
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(dir=build, prefix='linux-overlay-')
        cls.binary = Path(cls.temporary.name) / 'fenestra-file-overlay'
        compiler = shutil.which('cc') or shutil.which('clang')
        subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'include'), str(ROOT / 'linux/fenestra_file_overlay.c'),
                        str(ROOT / 'src/sha256.c'), str(ROOT / 'src/memory.c'),
                        '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_cli_rejects_invalid_digest(self):
        run = subprocess.run([str(self.binary), '/source', '/target', 'invalid', '/dev/shm'],
                             capture_output=True, text=True, timeout=10)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('SHA-256', run.stderr)

    @unittest.skipUnless(os.getenv('FENESTRA_TEST_MOUNTS') == '1',
                         'requires an isolated privileged mount namespace')
    def test_actual_path_substitution_on_read_only_lower(self):
        self.assertEqual(os.geteuid(), 0)
        with tempfile.TemporaryDirectory(dir='/dev/shm', prefix='fenestra-vfs-') as temp:
            root = Path(temp)
            lower = root / 'lower'
            lower.mkdir()
            original = b'mode=original\n'
            replacement = b'mode=card\n'
            target = lower / 'build.prop'
            target.write_bytes(original)
            source = root / 'replacement'
            source.write_bytes(replacement)
            digest = hashlib.sha256(replacement).hexdigest()
            subprocess.run(['mount', '--bind', str(lower), str(lower)], check=True)
            subprocess.run(['mount', '-o', 'remount,bind,ro', str(lower)], check=True)
            published = False
            old = target.open('rb')
            try:
                run = subprocess.run([str(self.binary), str(source), str(target), digest, str(root)],
                                     capture_output=True, text=True, timeout=15)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                published = True
                self.assertEqual(old.read(), original)
                self.assertEqual(target.read_bytes(), replacement)
                child = subprocess.check_output([sys.executable, '-c',
                    'from pathlib import Path; import sys; sys.stdout.buffer.write(Path(sys.argv[1]).read_bytes())',
                    str(target)])
                self.assertEqual(child, replacement)
                source.write_bytes(b'changed after publication')
                self.assertEqual(target.read_bytes(), replacement)
                with self.assertRaises(OSError) as error:
                    os.open(target, os.O_WRONLY)
                self.assertEqual(error.exception.errno, errno.EROFS)
                before = sorted(p.name for p in root.iterdir())
                bad = subprocess.run([str(self.binary), str(source), str(target), '00'*32, str(root)],
                                     capture_output=True, text=True, timeout=15)
                self.assertNotEqual(bad.returncode, 0)
                self.assertEqual(target.read_bytes(), replacement)
                self.assertEqual(sorted(p.name for p in root.iterdir()), before)
                subprocess.run(['umount', str(target)], check=True)
                published = False
                self.assertEqual(target.read_bytes(), original)
            finally:
                old.close()
                if published:
                    subprocess.run(['umount', '-l', str(target)], check=True)
                subprocess.run(['umount', '-l', str(lower)], check=True)
