import hashlib,shutil,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
import zf_c301_card_profile as profile
class ZfCardTests(unittest.TestCase):
    def test_real_zf_demo_package_staging_without_execution(self):
        cc=shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        if not cc:self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as d:
            build=ROOT/'build'/Path(d).name
            try:
                subprocess.run([sys.executable,str(ROOT/'host/build_sdk_module.py'),
                    str(ROOT/'modules/demo/sdk.c'),
                    '--id','demo-zf','--target','zf-c301','--output-dir',str(build)],
                    check=True,timeout=60)
                package=Path(d)/'demo-zf.wlm';package.write_bytes((build/'demo-zf.wlm').read_bytes())
            finally:
                if build.exists():shutil.rmtree(build)
            out=Path(d)/'stage-test'
            sources=['src/platform/zf_c301/module_stage.c','src/platform/zf_c301/card.c',
                'src/file_card.c','src/loader.c','src/card.c','src/module.c','src/runtime.c','src/sha256.c','src/exec.c','src/memory.c',
                'tests/zf_module_stage_fixture.c']
            subprocess.run([cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                '-I',str(ROOT/'include'),*[str(ROOT/x) for x in sources],'-o',str(out)],check=True)
            subprocess.run([str(out),str(package)],check=True)
    def test_read_only_scope_ownership_and_uncertain_errors(self):
        cc=shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        if not cc:self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'card-test'
            subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                '-I',str(ROOT/'include'),str(ROOT/'src/platform/zf_c301/card.c'),str(ROOT/'tests/test_zf_card.c'),'-o',str(out)],check=True)
            subprocess.run([str(out)],check=True)
            bad=subprocess.run([cc,'-std=c11','-I',str(ROOT/'include'),'-c',str(ROOT/'src/platform/zf_c301/card_native.c'),'-o',str(Path(d)/'bad.o')],capture_output=True)
            self.assertNotEqual(bad.returncode,0)
    def test_profile_guard_before_any_output(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);data=bytes(range(16));spec={'open':(0x40160000,16,hashlib.sha256(data).hexdigest())}
            with patch.multiple(profile,ROOT=root,PREIMAGES=spec),patch.object(profile,'_payload',return_value=data):
                result=profile.prepare('fixture',root/'build/valid')
                self.assertFalse(result['runtime_qualified'])
                self.assertEqual(result['native_open_flags'],[0,0])
                self.assertIn('FX_ZF_CARD_OPEN',(root/'build/valid/fx_zf_card_profile.h').read_text())
                with self.assertRaises(ValueError):profile.prepare('fixture',root/'build/valid')
                with self.assertRaises(ValueError):profile.prepare('fixture',root/'outside')
            with patch.multiple(profile,ROOT=root,PREIMAGES=spec),patch.object(profile,'_payload',return_value=data[:-1]+b'x'):
                with self.assertRaisesRegex(ValueError,'card helper differs'):profile.prepare('bad',root/'build/rejected')
                self.assertFalse((root/'build/rejected').exists())
if __name__=='__main__':unittest.main()
