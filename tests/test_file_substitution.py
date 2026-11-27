"""Run the compiled overlay and card adapter against a read-only fixture."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FileSubstitutionTests(unittest.TestCase):
    def test_staging_publication_and_open_lifetimes(self):
        compiler = shutil.which('clang') or shutil.which('gcc')
        self.assertIsNotNone(compiler, 'Clang or GCC is required')
        build = ROOT / 'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=build, prefix='overlay-') as tmp:
            binary = Path(tmp) / 'fixture.exe'
            built = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-Wconversion', '-Wshadow', '-I', 'include',
                                    'src/overlay.c', 'src/overlay_bulk.c', 'src/overlay_file.c',
                                    'src/overlay_service.c', 'src/sha256.c', 'src/memory.c',
                                    'tests/file_substitution_fixture.c', '-o', str(binary)],
                                   cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
