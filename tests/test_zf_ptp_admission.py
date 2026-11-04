"""Actual Zf admission plus shared transport, with controlled retained storage."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ZfPtpAdmissionTests(unittest.TestCase):
    def test_identity_consent_revocation_and_callback_exclusion(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('host compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'admission-test'
            args=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-pthread',
                  '-DFX_ZF_EXPERIMENTAL_LIFECYCLE=1',
                  '-I',str(ROOT/'include'),'-I',str(ROOT/'src/platform/zf_c301'),
                  str(ROOT/'src/platform/zf_c301/ptp_admission.c'),
                  str(ROOT/'src/platform/zf_c301/lifecycle.c'),
                  str(ROOT/'src/ptp.c'),str(ROOT/'src/memory.c'),
                  str(ROOT/'tests/zf_ptp_admission_fixture.c'),'-o',str(binary)]
            rejected=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(rejected.returncode,0)
            self.assertIn('experimental',rejected.stderr)
            subprocess.run([args[0],'-DFX_ZF_EXPERIMENTAL_PTP_ADMISSION=1',*args[1:]],check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=15)
if __name__=='__main__':unittest.main()
