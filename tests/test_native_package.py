"""Compile and execute a real native .wlm against explicit synthetic API stubs."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from module_header import validate_package,entry_abi
from platform_profiles import PROFILES
from export_sdk import export

class NativePackage(unittest.TestCase):
    def test_external_sdk_and_compiled_native_entry_on_both_profiles(self):
        if not shutil.which('clang') or not shutil.which('ld.lld'):self.skipTest('LLVM required')
        try:
            from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_PROT_READ,UC_PROT_WRITE,UC_PROT_EXEC
            from unicorn.arm64_const import UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_X4,UC_ARM64_REG_X5,UC_ARM64_REG_X30,UC_ARM64_REG_SP,UC_ARM64_REG_PSTATE
        except ImportError:self.skipTest('Unicorn required')
        with tempfile.TemporaryDirectory(prefix='fenestra-external-sdk-') as temporary:
            root=Path(temporary);sdk=root/'sdk';export(sdk)
            source=root/'project/module.c';source.parent.mkdir()
            # Keep real initialized data and more than the old 128 relocations
            # reachable from the entry, not just a permissive header fixture.
            sample=(ROOT/'examples/native-resident/module.c').read_text(encoding='utf-8')
            extra='static volatile unsigned char large[2*1024*1024] = {7};\n'
            extra+='static volatile unsigned char * volatile references[256] = {'+','.join(['&large[0]']*256)+'};\n'
            sample=sample.replace('int fenestra_native_entry(',extra+'int fenestra_native_entry(')
            marker='int fenestra_native_entry(const FxNativeBootstrap *api, FxNativeExports *exports) {'
            self.assertIn(marker,sample)
            sample=sample.replace(marker,marker+'\n    if (large[0] != 7u || references[255] != &large[0]) return -1;')
            source.write_text(sample,encoding='utf-8')
            for target in PROFILES:
                with self.subTest(target=target):
                    output=root/target
                    result=subprocess.run([sys.executable,'-B',str(sdk/'build_module.py'),str(source),'--native','--target',target,'--id','native_counter','--output-dir',str(output)],capture_output=True,text=True,timeout=60)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    blob=(output/'native_counter.wlm').read_bytes();identity=bytes.fromhex(PROFILES[target]['module_identity'])
                    info=validate_package(blob,identity);self.assertEqual(entry_abi(blob),2)
                    self.assertGreater(len(blob),1024*1024)
                    self.assertGreater(struct.unpack_from('<I',blob,104)[0],128)
                    wrong=bytearray(blob);wrong[8:12]=struct.pack('<I',3)
                    with self.assertRaises(ValueError):entry_abi(wrong)
                    base,stack,api,exports,stubs=0x1000000,0x4000000,0x5000000,0x5001000,0x6000000
                    cpu=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
                    cpu.mem_map(base,(info['image_bytes']+4095)&~4095)
                    count,relocs=struct.unpack_from('<II',blob,100)
                    segments=[struct.unpack_from('<6I',blob,128+i*24) for i in range(count)]
                    for offset,file_at,file_n,memory_n,flags,alignment in segments:
                        cpu.mem_write(base+offset,blob[file_at:file_at+file_n])
                    for i in range(relocs):
                        offset,kind,value=struct.unpack_from('<IIQ',blob,128+count*24+i*16)
                        self.assertEqual(kind,1);cpu.mem_write(base+offset,struct.pack('<Q',base+value))
                    for offset,file_at,file_n,memory_n,flags,alignment in segments:
                        start=offset&~4095;end=(offset+memory_n+4095)&~4095
                        cpu.mem_protect(base+start,end-start,(UC_PROT_READ if flags&1 else 0)|(UC_PROT_WRITE if flags&2 else 0)|(UC_PROT_EXEC if flags&4 else 0))
                    cpu.mem_map(stack,65536);cpu.mem_map(api,8192);cpu.mem_map(stubs,4096)
                    ret=struct.pack('<I',0xd65f03c0)
                    for offset in (0,16,32,48,64,80,96):cpu.mem_write(stubs+offset,ret)
                    cpu.mem_write(api,struct.pack('<4I3Q32s7Q',1,128,1,1,0,0,0,identity,*[stubs+i*16 for i in range(7)]))
                    cpu.mem_write(exports,struct.pack('<4I2Q',1,32,2,0,0,0))
                    observed={'ui':[],'removed':0}
                    def read64(address):return struct.unpack('<Q',cpu.mem_read(address,8))[0]
                    def callback(uc,address,size,context):
                        if address==stubs+48:
                            declaration=uc.reg_read(UC_ARM64_REG_X1);name=read64(declaration)
                            observed['name']=bytes(uc.mem_read(name,32)).split(b'\0')[0]
                            observed['handler']=read64(declaration+32)
                            uc.mem_write(uc.reg_read(UC_ARM64_REG_X2),struct.pack('<Q',9));uc.reg_write(UC_ARM64_REG_X0,0)
                        elif address==stubs+16:
                            request=uc.reg_read(UC_ARM64_REG_X1);reply=uc.reg_read(UC_ARM64_REG_X2)
                            self.assertEqual(struct.unpack('<I',uc.mem_read(request+4,4))[0],23)
                            observed['ui'].append(struct.unpack('<I',uc.mem_read(request+48+8,4))[0]);uc.mem_write(reply,bytes(816));uc.reg_write(UC_ARM64_REG_X0,0)
                        elif address==stubs+64:
                            self.assertEqual(uc.reg_read(UC_ARM64_REG_X1),9);observed['removed']+=1;uc.reg_write(UC_ARM64_REG_X0,0)
                    cpu.hook_add(UC_HOOK_CODE,callback)
                    cpu.reg_write(UC_ARM64_REG_PSTATE,5)
                    def invoke(address,args):
                        cpu.reg_write(UC_ARM64_REG_SP,stack+65536-16);cpu.reg_write(UC_ARM64_REG_X30,stubs+0xff0)
                        for reg,value in zip([UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_X4,UC_ARM64_REG_X5],args):cpu.reg_write(reg,value)
                        cpu.emu_start(address,stubs+0xff0,count=100000)
                        self.assertEqual(cpu.reg_read(UC_ARM64_REG_X0),0)
                    invoke(base+info['entry_offset'],[api,exports])
                    self.assertEqual(observed['name'],b'example.counter');self.assertEqual(observed['ui'],[1,2])
                    caller,reply=api+512,api+1024;cpu.mem_write(caller,struct.pack('<4I',1,16,7,1))
                    for n in (1,2):
                        invoke(observed['handler'],[0,caller,0,0,0,reply])
                        self.assertEqual(read64(reply+16),n);self.assertEqual(read64(reply+24),7)
                    invoke(read64(exports+24),[0,1]);self.assertEqual(observed['removed'],1)

if __name__=='__main__':unittest.main()
