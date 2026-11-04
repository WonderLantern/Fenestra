"""Synthetic tests for the unintegrated, default-off Zf PTP adapter."""
import binascii
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
PLATFORM=ROOT/'src/platform/zf_c301'
SPEC=importlib.util.spec_from_file_location('zf_ptp_displaced',PLATFORM/'ptp_displaced.py')
DISPLACED=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DISPLACED)


class ZfPtpAdapterTests(unittest.TestCase):
    def test_shared_serializer_allows_in_place_operation_array_mutation(self):
        compiler = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not compiler:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'alias.c'
            binary = root / 'alias.exe'
            source.write_text('''#include "fenestra_ptp.h"\n#include <stdint.h>\nint main(void){uint8_t b[512]={0}; b[0]=1; b[4]=1; b[5]=0x10; return fx_ptp_extend_operations(b,512,b,512)?0:1;}\n''')
            subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),
                            str(ROOT/'src/ptp_device_info.c'),str(ROOT/'src/ptp.c'),str(ROOT/'src/memory.c'),str(source),'-o',str(binary)],
                           check=True,timeout=30)
            subprocess.run([str(binary)],check=True,timeout=10)

    def test_default_off_and_explicit_policy_gate(self):
        compiler=shutil.which('cc') or shutil.which('gcc')
        if not compiler:self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            for value in (None,0,1,2,'unsupported'):
                output=Path(tmp)/('adapter-'+str(value)+'.exe')
                flags=[] if value is None else ['-DFX_ZF_C301_PTP_EXPERIMENT='+str(value)]
                command=[compiler,'-std=c11','-Wall','-Wextra','-Werror',*flags,
                         '-I',str(ROOT/'include'),'-I',str(PLATFORM),
                         str(PLATFORM/'ptp.c'),str(ROOT/'tests/zf_ptp_adapter_fixture.c'),'-o',str(output)]
                if value in (2,'unsupported'):
                    result=subprocess.run(command,capture_output=True,text=True,timeout=30)
                    self.assertNotEqual(result.returncode,0)
                    if value==2:self.assertIn('FX_ZF_C301_PTP_EXPERIMENT must be 0 or 1',result.stderr)
                    self.assertFalse(output.exists())
                    continue
                subprocess.run(command,check=True,capture_output=True,timeout=30)
                result=subprocess.run([str(output)],check=True,capture_output=True,text=True,timeout=10)
                self.assertEqual(result.stdout.strip(),'PASS zf_ptp_adapter')

    def test_experimental_adapter_has_no_writable_static_sections(self):
        compiler=shutil.which('cc') or shutil.which('gcc')
        size=shutil.which('size')
        if not compiler or not size:self.skipTest('host object section tools unavailable')
        with tempfile.TemporaryDirectory() as directory:
            obj=Path(directory)/'adapter.o'
            subprocess.run([compiler,'-std=c11','-O2','-fno-pie','-fno-pic',
                '-DFX_ZF_C301_PTP_EXPERIMENT=1','-I',str(ROOT/'include'),'-I',str(PLATFORM),
                '-c',str(PLATFORM/'ptp.c'),'-o',str(obj)],check=True,timeout=30)
            sections=subprocess.check_output([size,'-A',str(obj)],text=True)
            for line in sections.splitlines():
                fields=line.split()
                if len(fields)>=2 and fields[0].startswith(('.data','.bss')):
                    self.assertEqual(int(fields[1]),0,line)

    def test_displaced_identity_output_and_rejection(self):
        payload=bytes(range(32))
        full=payload+binascii.crc_hqx(payload,0).to_bytes(2,'big')
        offsets={'lookup':0,'session':8,'operations':16}
        sites={'fx_zf_ptp_original_'+name:(DISPLACED.BASE+at,hashlib.sha256(full[at:at+16]).hexdigest())
               for name,at in offsets.items()}
        fields=dict(FULL_SIZE=len(full),FULL_SHA256=hashlib.sha256(full).hexdigest(),
                    PAYLOAD_SHA256=hashlib.sha256(payload).hexdigest(),SITES=sites)
        with tempfile.TemporaryDirectory() as tmp,patch.multiple(DISPLACED,**fields):
            base=Path(tmp);original=base/'original';original.write_bytes(full)
            output=base/'private';report=DISPLACED.prepare(original,output)
            self.assertFalse(report['runtime_qualified'])
            self.assertFalse(report['firmware_output_generated'])
            for name,at in offsets.items():
                data=full[at:at+16]
                self.assertEqual((output/('fx_zf_ptp_original_'+name+'.inc')).read_text(),
                                 '.byte '+','.join(map(str,data))+'\n')
            with self.assertRaises(ValueError):DISPLACED.prepare(original,output)
            bad=base/'bad';bad.write_bytes(full[:-1]+bytes([full[-1]^1]))
            rejected=base/'rejected'
            with self.assertRaises(ValueError):DISPLACED.prepare(bad,rejected)
            self.assertFalse(rejected.exists())
            with self.assertRaises(ValueError):DISPLACED.prepare(original,ROOT/'forbidden-private-output')


if __name__=='__main__':unittest.main()
