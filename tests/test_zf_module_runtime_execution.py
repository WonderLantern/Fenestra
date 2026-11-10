"""Require an explicit live execution arena before module entry invocation."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ZfModuleRuntimeExecutionTests(unittest.TestCase):
    def test_unmapped_module_is_never_activated_or_invoked(self):
        cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:
            self.skipTest('host compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'module-runtime-execution-test'
            command = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion',
                       '-DFX_ZF_EXPERIMENTAL_MODULE_RUNTIME=1',
                       '-I', str(ROOT / 'include'), '-I', str(ROOT / 'src/platform/zf_c301'),
                       str(ROOT / 'src/platform/zf_c301/module_runtime.c'),
                       str(ROOT / 'tests/zf_module_runtime_execution_fixture.c'),
                       '-o', str(binary)]
            subprocess.run(command, check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
