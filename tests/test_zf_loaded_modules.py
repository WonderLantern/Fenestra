"""Use actual platform image binder, not a fake loaded boolean."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import sys
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(sys.platform.startswith("linux"), "fixture requires ELF dead-section linking")
class LoadedModulesTests(unittest.TestCase):
    def test_only_successful_prepared_binding_is_published_and_retained(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('host compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'resident-test'
            cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                 '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                 '-DFX_ZF_EXPERIMENTAL_LOADED_MODULES=1',
                 '-DFX_ZF_C301_EXPERIMENTAL_SERVICES=1',
                 '-I',str(ROOT/'include'),'-I',str(ROOT/'src/platform/zf_c301'),
                 str(ROOT/'src/platform/zf_c301/loaded_modules.c'),str(ROOT/'src/platform.c'),
                 str(ROOT/'src/platform/zf_c301/services.c'),str(ROOT/'src/services.c'),
                 str(ROOT/'src/worker_wait.c'),str(ROOT/'src/accounting.c'),
                 str(ROOT/'src/ui_dispatch.c'),str(ROOT/'src/tasks.c'),
                 str(ROOT/'src/binding_store.c'),str(ROOT/'src/capacity.c'),str(ROOT/'src/slot_pool.c'),
                 str(ROOT/'src/native_call.c'),str(ROOT/'src/memory.c'),str(ROOT/'tests/zf_loaded_modules_fixture.c'),'-o',str(binary)]
            subprocess.run(cmd,check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=10)
if __name__=='__main__':unittest.main()
