"""Experimental Zf dispatcher gate and local-only trampoline input checks."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
import zf_c301_sysctl as sysctl


class ZfSysctlTests(unittest.TestCase):
    def test_checked_displaced_window_and_no_overwrite(self):
        image = bytes(range(256)) + bytes(range(44))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / 'input'
            original.write_bytes(image)
            kwargs = dict(ROOT=root, EG_SIZE=len(image) + 2,
                          EG_PAYLOAD_SHA256=hashlib.sha256(image).hexdigest(),
                          SYSCTL_DISPATCH=sysctl.BASE,
                          PREFIX_SHA256=hashlib.sha256(image[:16]).hexdigest(),
                          SPAN_SHA256=hashlib.sha256(image[:268]).hexdigest())
            with patch.multiple(sysctl, **kwargs):
                out = root / 'build/valid'
                result = sysctl.prepare(original, out)
                self.assertFalse(result['runtime_qualified'])
                self.assertFalse(result['installation_hook_generated'])
                self.assertEqual((out / 'fx_zf_system_original.inc').read_text(),
                                 '.byte ' + ','.join(str(b) for b in image[:16]) + '\n')
                with self.assertRaises(ValueError):
                    sysctl.prepare(original, out)
                with self.assertRaises(ValueError):
                    sysctl.prepare(original, root / 'outside')
                original.write_bytes(image[:-1] + bytes([image[-1] ^ 1]))
                with self.assertRaises(ValueError):
                    sysctl.prepare(original, root / 'build/rejected')
                self.assertFalse((root / 'build/rejected').exists())

    def test_experimental_gate_preserves_forwarding_and_event_order(self):
        compiler = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        if not compiler:
            self.skipTest('host C compiler is unavailable')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'gate-test.exe'
            gate = ROOT / 'src/platform/zf_c301/sysctl_gate.c'
            common = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
                      '-I', str(ROOT / 'include')]
            rejected = subprocess.run([*common, '-c', str(gate), '-o', str(output)], capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(output.exists())
            rejected = subprocess.run([*common, '-DFX_ZF_EXPERIMENTAL_SYSCTL_GATE=0', '-c', str(gate),
                                       '-o', str(output)], capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(output.exists())
            subprocess.run([*common, '-DFX_ZF_EXPERIMENTAL_SYSCTL_GATE', str(gate),
                            str(ROOT / 'tests/test_zf_sysctl_gate.c'), str(ROOT / 'src/memory.c'),
                            '-o', str(output)], check=True, timeout=30)
            subprocess.run([str(output)], check=True, timeout=10)

            subprocess.run([*common, '-DFX_ZF_EXPERIMENTAL_SYSCTL_GATE=1',
                            '-DFX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP=1',
                            '-DFX_ZF_EXPERIMENTAL_OWNER_WORKER=1', str(gate),
                            str(ROOT / 'tests/test_zf_sysctl_gate.c'), str(ROOT / 'src/memory.c'),
                            '-o', str(output)], check=True, timeout=30)
            subprocess.run([str(output)], check=True, timeout=10)
            subprocess.run([*common, '-DFX_ZF_EXPERIMENTAL_SYSCTL_GATE=1',
                            '-DFX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP=1',
                            '-DFX_ZF_EXPERIMENTAL_OWNER_WORKER=1',
                            '-DFX_ZF_EXPERIMENTAL_MENU_NATIVE=1',str(gate),
                            str(ROOT / 'tests/test_zf_sysctl_gate.c'), str(ROOT / 'src/memory.c'),
                            '-o', str(output)], check=True, timeout=30)
            subprocess.run([str(output)], check=True, timeout=10)
            subprocess.run([*common, '-DFX_ZF_EXPERIMENTAL_SYSCTL_GATE=1',
                            '-DFX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP=1', str(gate),
                            str(ROOT / 'tests/test_zf_sysctl_gate.c'), str(ROOT / 'src/memory.c'),
                            '-o', str(output)], check=True, timeout=30)
            subprocess.run([str(output)], check=True, timeout=10)

    def test_original_may_release_message_before_notification(self):
        compiler = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        if not compiler:
            self.skipTest('host C compiler is unavailable')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'gate-asan.exe'
            command = [compiler, '-std=c11', '-fno-builtin', '-fsanitize=address', '-fno-omit-frame-pointer',
                       '-DFX_ZF_EXPERIMENTAL_SYSCTL_GATE=1', '-I', str(ROOT / 'include'),
                       str(ROOT / 'src/platform/zf_c301/sysctl_gate.c'),
                       str(ROOT / 'tests/test_zf_sysctl_gate.c'), str(ROOT / 'src/memory.c'), '-o', str(output)]
            compiled = subprocess.run(command, capture_output=True, timeout=30)
            if compiled.returncode:
                self.skipTest('AddressSanitizer is unavailable')
            subprocess.run([str(output)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
