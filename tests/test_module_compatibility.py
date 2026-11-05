"""Independent Python checks versus the real C prepare/discovery/manager path."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from module_header import validate_package
from module_compatibility import CompatibilityError,encode,parse_requirement,validate_environment,export_core
from pack_module import pack

IDENTITY=b'\x11'*32
OUTPUT=dict(name='test.output',kind=4,abi=4,version=1)


def package(version=4,requirements=None,required=0x200000,service=1):
    header=bytearray(128);header[:8]=b'FENESTRA'
    struct.pack_into('<4I',header,8,version,128,1,1);header[24:56]=IDENTITY
    if version>=2:struct.pack_into('<I',header,116,1)
    if version>=3:struct.pack_into('<2I',header,120,65536,65536)
    body=struct.pack('<6I',0,152,4,4096,5,4096)+bytes.fromhex('c0035fd6')
    if version==4:body+=encode([OUTPUT] if requirements is None else requirements,service)
    header[56:88]=hashlib.sha256(body).digest()
    struct.pack_into('<5IQ',header,88,128+len(body),4096,0,1,0,required)
    return bytes(header)+body


def reseal(data):
    data=bytearray(data);data[56:88]=hashlib.sha256(data[128:]).digest();struct.pack_into('<I',data,88,len(data));return bytes(data)


def environment(mode='normal'):
    value=dict(schema='fenestra.module-environment/v1',module_identity=IDENTITY.hex(),core_abi=1,isa=1,
        service_abi=2 if mode=='service' else 1,capabilities=0x10000 if mode=='limited' else 0x7f0000,module_formats=[1,2,3,4],interfaces=[
            dict(**OUTPUT,available=True,capabilities=0x200000),
            dict(name='test.function',kind=1,abi=1,version=2,available=True,capabilities=0x280000),
            dict(name='test.table',kind=2,abi=2,version=3,available=True,capabilities=0x200000)])
    if mode=='missing':value['interfaces']=[]
    if mode in ('kind','abi','version'):value['interfaces'][0][mode]=3 if mode=='kind' else 2
    if mode=='unavailable':value['interfaces'][0]['available']=False
    if mode=='foreign-environment':value['module_identity']='00'*32
    return value


class CompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=shutil.which('clang') or shutil.which('gcc')
        if not compiler:raise RuntimeError('C11 compiler required')
        (ROOT/'build').mkdir(exist_ok=True)
        cls.temp=tempfile.TemporaryDirectory(prefix='compatibility-',dir=ROOT/'build');cls.addClassCleanup(cls.temp.cleanup)
        cls.directory=Path(cls.temp.name);cls.binary=cls.directory/'check.exe'
        run=subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
            '-DFX_PLATFORM_PROFILE=1','-I','include','src/module.c','src/memory.c','src/sha256.c','src/services.c',
            'src/platform.c','src/binding_store.c','src/resource_conflict.c','src/native_call.c','src/module_manager.c','tests/module_compatibility_fixture.c',
            '-o',str(cls.binary)],cwd=ROOT,capture_output=True,text=True,timeout=60)
        if run.returncode:raise AssertionError(run.stdout+run.stderr)

    def write(self,data,name='test.wlm'):
        path=self.directory/name;path.write_bytes(data);return path

    def native(self,data,mode='normal'):
        result=subprocess.run([str(self.binary),str(self.write(data)),mode],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return json.loads(result.stdout)

    def compare(self,data,reason=0,mode='normal'):
        c=self.native(data,mode)
        self.assertEqual(c['reason'],reason,c)
        if reason:
            with self.assertRaises(CompatibilityError) as caught:
                validate_package(data,IDENTITY,capabilities=environment(mode)['capabilities'],environment=environment(mode))
            self.assertEqual(caught.exception.issue['code'],reason)
            self.assertEqual(caught.exception.issue['binding'],c['binding'])
            self.assertEqual(caught.exception.issue['expected'],c['expected'])
            self.assertEqual(caught.exception.issue['actual'],c['actual'])
        else:
            validate_package(data,IDENTITY,environment=environment(mode));self.assertEqual(c['result'],0)
        return c

    def test_v1_v2_v3_packages_keep_their_existing_contract(self):
        for version in (1,2,3):
            with self.subTest(version=version):self.compare(package(version=version))

    def test_v4_exact_interface_checked_without_opening_resources(self):
        c=self.compare(package());self.assertEqual(c['lookups'],1)
        self.assertTrue(validate_package(package(),IDENTITY,environment=environment())['compatibility_checked'])
        self.assertFalse(validate_package(package(),IDENTITY)['compatibility_checked'])

    def test_missing_wrong_type_abi_version_and_unavailable_interface(self):
        for mode,reason in (('missing',12),('kind',13),('abi',14),('version',15),('unavailable',16)):
            with self.subTest(mode=mode):self.compare(package(),reason,mode)

    def test_required_service_and_capabilities(self):
        self.compare(package(),11,'service');self.compare(package(),7,'limited')
        self.compare(package(version=2),11,'service')
        self.compare(package(requirements=[dict(name='test.function',kind=1,abi=1,version=2)]),7)

    def test_target_core_isa_and_format_mismatch(self):
        for offset,value,reason in ((8,5,2),(16,2,3),(20,2,4),(116,2,8)):
            with self.subTest(offset=offset):
                data=bytearray(package());struct.pack_into('<I',data,offset,value);self.compare(bytes(data),reason)
        data=bytearray(package());data[24]^=1;self.compare(bytes(data),5)
        for version in (2,4):self.compare(package(version=version),5,'foreign-environment')

    def test_checksum_failure_never_mutates_image_or_looks_up_interfaces(self):
        data=bytearray(package());data[152]^=1
        self.assertEqual(self.compare(bytes(data),6)['lookups'],0)

    def test_no_environment_refuses_v4_before_image_copy(self):
        c=self.native(package(),'no-environment');self.assertEqual(c['reason'],10);self.assertEqual(c['lookups'],0)
        c=self.native(package(version=2),'no-environment');self.assertEqual(c['result'],0)

    def test_malformed_footer_names_and_duplicate_requirements(self):
        original=package()
        variants=[]
        for offset,value in ((len(original)-24,0),(len(original)-16,257),(len(original)-12,81),(len(original)-4,1),(156,0),(156+76,1)):
            data=bytearray(original)
            if offset in (len(original)-24,156):data[offset]=value
            else:struct.pack_into('<I',data,offset,value)
            variants.append(reseal(data))
        data=bytearray(original);data[156+63]=1;variants.append(reseal(data))
        for data in variants:
            with self.subTest(data=hashlib.sha256(data).hexdigest()):self.assertEqual(self.compare(data,9)['lookups'],0)
        data=bytearray(package(requirements=[OUTPUT,dict(name='other',kind=4,abi=4,version=1)]))
        data[236:300]=data[156:220];self.assertEqual(self.compare(reseal(data),9)['lookups'],0)

    def test_metadata_cannot_be_loaded_as_module_code(self):
        data=bytearray(package());struct.pack_into('<I',data,132,156)
        c=self.native(reseal(data));self.assertEqual(c['reason'],1);self.assertEqual(c['lookups'],0)
        with self.assertRaises(ValueError):validate_package(reseal(data),IDENTITY,environment=environment())

    def test_256_named_requirements_without_resident_contract_copy(self):
        rows=[dict(name='test.%03d'%i,kind=4,abi=4,version=1) for i in range(256)]
        e=environment();e['interfaces']=[dict(**row,available=True,capabilities=0x200000) for row in rows]
        result=validate_package(package(requirements=rows),IDENTITY,environment=e)
        self.assertEqual(len(result['requirements']),256)
        c=self.native(package(requirements=rows),'many');self.assertEqual(c['result'],0);self.assertEqual(c['lookups'],256)

    def test_packer_emits_v4_footer_and_preserves_explicit_stacks(self):
        elf=bytearray(132);elf[:7]=b'\x7fELF\x02\x01\x01'
        struct.pack_into('<HHIQQQIHHHHHH',elf,16,3,183,1,0,64,0,0,64,56,1,64,0,0)
        struct.pack_into('<IIQQQQQQ',elf,64,1,5,128,0,0,4,4096,1);elf[128:]=bytes.fromhex('c0035fd6')
        for requirements,stacks,version in ((None,{},2),(None,dict(native_stack_bytes=32768,user_stack_bytes=4096),3),
            ([OUTPUT],dict(native_stack_bytes=32768,user_stack_bytes=4096),4),([],{},4)):
            data=pack(bytes(elf),IDENTITY,0x200000,entry_abi=1,requirements=requirements,**stacks)
            self.assertEqual(struct.unpack_from('<I',data,8)[0],version)
            self.compare(data)
            info=validate_package(data,IDENTITY,environment=environment())
            if stacks:self.assertEqual(info['user_stack_bytes'],4096)

    def test_old_core_profile_refuses_new_format(self):
        e=environment();e['module_formats']=[1,2,3]
        with self.assertRaises(CompatibilityError) as caught:validate_package(package(),IDENTITY,environment=e)
        self.assertEqual(caught.exception.issue['code'],2)

    def test_export_reads_actual_registry_without_addresses_or_instruction_bytes(self):
        base=0x401a0000;data=bytearray(2048)
        metadata=[0x4658434d50543031,1,base+512,4,128,56,56,0,8,12,16,20,24,32,72,88,96,120,1,1,1,0x7f0000,30]
        struct.pack_into('<23Q',data,0,*metadata);struct.pack_into('<I',data,516,1)
        data[640:672]=IDENTITY;struct.pack_into('<Q',data,568,base+1024)
        for at in (72,88,96,120):struct.pack_into('<Q',data,512+at,1)
        struct.pack_into('<Q6IQ16s',data,1024,base+1536,4,4,1,8,0x200000,0,0xdeadbeef,b'\xde'*16)
        data[1536:1548]=b'test.output\0'
        value=export_core(bytes(data),{'fx_module_compatibility_metadata':base},base,'fixture')
        self.assertEqual(value['interfaces'],[dict(**OUTPUT,available=True,capabilities=0x200000)])
        self.assertEqual(value['module_identity'],IDENTITY.hex())
        self.assertNotIn('deadbeef',json.dumps(value));self.assertNotIn('dededede',json.dumps(value))

    def test_public_cli_reports_structured_failure(self):
        profile=self.directory/'compatibility.json';profile.write_text(json.dumps(environment()))
        for data,code in ((package(),0),(package(requirements=[dict(name='absent.output',kind=4,abi=4,version=1)]),1)):
            result=subprocess.run([sys.executable,'-B',str(ROOT/'host/check_module.py'),str(self.write(data)),'--core-profile',str(profile)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,code,result.stdout+result.stderr)
            self.assertEqual(json.loads(result.stdout)['compatible'],code==0)

    def test_discovery_skips_only_bad_modules_and_menu_explains_why(self):
        good=self.write(package(version=2),'good.wlm')
        bad=bytearray(package());bad[24]^=1;wrong=self.write(bytes(bad),'wrong.wlm')
        missing=self.write(package(requirements=[dict(name='absent.output',kind=4,abi=4,version=1)]),'missing.wlm')
        run=subprocess.run([str(self.binary),'inventory',str(good),str(wrong),str(missing)],capture_output=True,text=True,timeout=10)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr);self.assertTrue(json.loads(run.stdout)['card_errors_preserved'])

    def test_profile_and_packer_argument_validation(self):
        self.assertEqual(parse_requirement('test.output:device:device:1'),OUTPUT)
        for value in ('bad','test.output:device:device:0','test.output:bogus:bytes:1'):
            with self.assertRaises(ValueError):parse_requirement(value)
        with self.assertRaises(ValueError):encode([OUTPUT,OUTPUT])
        e=environment();e['interfaces'].append(copy.deepcopy(e['interfaces'][0]))
        with self.assertRaises(ValueError):validate_environment(e)


if __name__=='__main__':unittest.main()
