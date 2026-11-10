"""Task ownership, exclusion ordering and navigation error propagation."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class AppsFlushTests(unittest.TestCase):
    def test_navigation_occurs_only_after_release_on_captured_task(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('host compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            (path/'fx_zf_dispatch_profile.h').write_text('/* fixture addresses defined by test */\n')
            cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                 '-DFX_ZF_EXPERIMENTAL_APPS_FLUSH=1','-I',str(ROOT/'include'),'-I',str(path),
                 str(ROOT/'tests/zf_apps_flush_fixture.c'),'-o',str(path/'test')]
            subprocess.run(cmd,check=True,timeout=30)
            subprocess.run([str(path/'test')],check=True,timeout=10)
if __name__=='__main__':unittest.main()
