"""Publish only a real initialized Apps controller with Zf event contract."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import sys
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(sys.platform.startswith("linux"), "fixture requires ELF dead-section linking")
class AppsDomainTests(unittest.TestCase):
    def test_initialization_precedes_publication_and_controller_is_not_replaced(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('host compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'apps-test'
            command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                '-DFX_ZF_EXPERIMENTAL_APPS_DOMAIN=1','-I',str(ROOT/'include'),
                '-I',str(ROOT/'src/platform/zf_c301'),str(ROOT/'src/platform/zf_c301/apps_domain.c'),
                str(ROOT/'src/native_apps.c'),str(ROOT/'src/memory.c'),
                str(ROOT/'tests/zf_apps_domain_fixture.c'),'-o',str(output)]
            subprocess.run(command,check=True,timeout=30)
            subprocess.run([str(output)],check=True,timeout=10)
if __name__=='__main__':unittest.main()
