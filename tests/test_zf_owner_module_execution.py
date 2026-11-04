"""Host simulation of the Zf owner-to-module lifecycle and safe return."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ZfOwnerModuleExecutionTests(unittest.TestCase):
    def test_owner_runs_entry_and_returns_through_stop(self):
        cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:
            self.skipTest('host compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'zf-owner-module-execution'
            command = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion',
                       '-DFX_ZF_EXPERIMENTAL_OWNER_TASK=1',
                       '-DFX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1',
                       '-DFX_ZF_EXPERIMENTAL_MODULE_RUNTIME=1',
                       '-DFX_ZF_EXPERIMENTAL_LIFECYCLE=1',
                       '-DFX_ZF_C301_EXPERIMENTAL_TASK=1',
                       '-I', str(ROOT / 'include'), '-I', str(ROOT / 'src/platform/zf_c301'),
                       str(ROOT / 'src/platform/zf_c301/owner_task.c'),
                       str(ROOT / 'src/platform/zf_c301/module_runtime.c'),
                       str(ROOT / 'src/platform/zf_c301/lifecycle.c'),
                       str(ROOT / 'tests/zf_owner_module_execution_fixture.c'),
                       '-o', str(binary)]
            subprocess.run(command, check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
