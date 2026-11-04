"""Exercise unchanged demo code with Zf copied-message and shared UI logic."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import sys

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(sys.platform.startswith("linux"), "fixture requires ELF dead-section linking")
class ZfDemoServicesTests(unittest.TestCase):
    def test_original_demo_receives_replies_and_submits_frames(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('host C compiler unavailable')
        sources=['tests/zf_demo_services_fixture.c','modules/demo/sdk.c',
                 'src/platform/zf_c301/services.c','src/services.c','src/platform.c',
                 'src/ui_dispatch.c','src/tasks.c','src/binding_store.c',
                 'src/capacity.c','src/slot_pool.c','src/native_call.c','src/memory.c','src/worker_wait.c','src/accounting.c']
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'demo-services'
            subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                '-DFX_ZF_C301_EXPERIMENTAL_SERVICES=1','-I',str(ROOT/'include'),
                '-I',str(ROOT/'src/platform/zf_c301'),*[str(ROOT/p) for p in sources],
                '-o',str(binary)],check=True,timeout=40)
            subprocess.run([str(binary)],check=True,timeout=10)

if __name__=='__main__':unittest.main()
