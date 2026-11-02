from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class ZfMenuModelTests(unittest.TestCase):
    def test_retained_pages_and_labels(self):
        cc=shutil.which('cc') or shutil.which('gcc')
        if not cc:self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/'menu'
            subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-fno-builtin',
                '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                '-DFX_ZF_EXPERIMENTAL_MENU_SETUP=1','-I',str(ROOT/'include'),
                '-I',str(ROOT/'src/platform/zf_c301'),
                *[str(ROOT/p) for p in ['tests/test_zf_menu_model.c','src/platform/zf_c301/menu_model.c',
                    'src/platform/zf_c301/menu_setup.c','src/native_menu.c','src/memory.c']],
                '-o',str(exe)],check=True,timeout=30)
            subprocess.run([str(exe)],check=True,timeout=10)
