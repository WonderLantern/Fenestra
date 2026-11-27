"""Pack real multi-version recipes and consume them through the C reader."""
import copy
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
from range_substitution import pack


class RangeSubstitutionTests(unittest.TestCase):
    def test_multiple_firmware_layouts_and_native_reads(self):
        compiler = shutil.which('clang') or shutil.which('gcc')
        self.assertIsNotNone(compiler)
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='ranges-') as tmp:
            directory = Path(tmp)
            original = bytes((i * 31) % 256 for i in range(81920))
            (directory / 'original.bin').write_bytes(original)
            (directory / 'a.bin').write_bytes(b'ABCD')
            (directory / 'b.bin').write_bytes(b'12345678')
            manifest = {'schema': 'fenestra.range-substitution/v1', 'versions': [
                {'application_sha256': '11' * 32, 'original': 'original.bin',
                 'region': {'start': 4096, 'end': 74096},
                 'segments': [{'start': 8190, 'end': 8194, 'file': 'a.bin'},
                              {'start': 69628, 'end': 69636, 'file': 'b.bin'}]},
                {'application_sha256': '22' * 32, 'original': 'original.bin',
                 'region': {'start': 2048, 'end': 62048},
                 'segments': [{'start': 4095, 'end': 4099, 'file': 'a.bin'}]}
            ]}
            recipe = pack(manifest, directory)
            self.assertLess(len(recipe), 4096)
            (directory / 'patch.fnr').write_bytes(recipe)
            executable = directory / 'fixture.exe'
            command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wconversion',
                       '-Wshadow', '-I', 'include', 'src/overlay_ranges.c', 'src/sha256.c',
                       'src/memory.c', 'tests/overlay_ranges_fixture.c', '-o', str(executable)]
            built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            actual = directory / 'actual.bin'
            second = directory / 'second.bin'
            run = subprocess.run([str(executable), str(directory / 'patch.fnr'),
                                  str(directory / 'original.bin'), str(actual), str(second)],
                                 capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            for version, output in zip(manifest['versions'], [actual, second]):
                start, end = version['region']['start'], version['region']['end']
                expected = bytearray(original[start:end])
                for segment in version['segments']:
                    expected[segment['start']-start:segment['end']-start] = (
                        directory / segment['file']).read_bytes()
                self.assertEqual(output.read_bytes(), bytes(expected))
            self.assertEqual((directory / 'original.bin').read_bytes(), original)
            self.assertEqual((directory / 'patch.fnr').read_bytes(), recipe)
            duplicate = copy.deepcopy(manifest)
            duplicate['versions'][1]['application_sha256'] = '11' * 32
            with self.assertRaises(ValueError):
                pack(duplicate, directory)
            overlapping = copy.deepcopy(manifest)
            overlapping['versions'][0]['segments'].append(
                {'start': 8191, 'end': 8195, 'file': 'a.bin'})
            with self.assertRaises(ValueError):
                pack(overlapping, directory)
            bad_hash = copy.deepcopy(manifest)
            bad_hash['versions'][0]['original_sha256'] = hashlib.sha256(b'wrong').hexdigest()
            with self.assertRaises(ValueError):
                pack(bad_hash, directory)
