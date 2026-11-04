"""Execute only the compiled core's preflight/copy function in Unicorn.

No module entry, original firmware function, device or complete camera system
is mapped. Requires the optional Python unicorn package and a local core build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from module_compatibility import export_core


def check(build, package):
    from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC
    from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_SP, UC_ARM64_REG_LR, UC_ARM64_REG_PC
    report=json.loads((build/'build.json').read_text(encoding='utf-8'))
    symbols={name:int(value,0) if isinstance(value,str) else value for name,value in report['symbols'].items()}
    code_path=build/('core.bin' if (build/'core.bin').exists() else 'menu.bin')
    code=code_path.read_bytes();base=0x401a0000
    profile=export_core(code,symbols,base,report.get('target','zr-c110'))
    if len(package)<128 or len(package)>1048576:raise ValueError('bounded module package required')
    image_bytes=struct.unpack_from('<I',package,92)[0]
    capacity=min(4194304,max(4096,(image_bytes+4095)&~4095))
    uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    uc.mem_map(base,(len(code)+4095)&~4095);uc.mem_write(base,code)
    uc.mem_protect(base,(len(code)+4095)&~4095,UC_PROT_READ|UC_PROT_EXEC)
    source,aux,image,stack,returned=0x70000000,0x70200000,0x71000000,0x72000000,0x73000000
    uc.mem_map(source,1048576);uc.mem_write(source,package);uc.mem_protect(source,1048576,UC_PROT_READ)
    uc.mem_map(aux,4096);uc.mem_write(aux,hashlib.sha256(package).digest())
    uc.mem_write(aux+32,bytes.fromhex(profile['module_identity']))
    uc.mem_map(image,capacity,UC_PROT_READ|UC_PROT_WRITE);uc.mem_write(image,b'\xa5'*capacity)
    uc.mem_map(stack,65536,UC_PROT_READ|UC_PROT_WRITE);uc.mem_map(returned,4096,UC_PROT_READ|UC_PROT_EXEC)
    sp=stack+65520;module,issue=aux+128,aux+1024
    uc.mem_write(sp,struct.pack('<Q',issue));uc.reg_write(UC_ARM64_REG_SP,sp);uc.reg_write(UC_ARM64_REG_LR,returned)
    for i,value in enumerate((source,len(package),aux,aux+32,profile['capabilities'],image,capacity,module)):
        uc.reg_write(UC_ARM64_REG_X0+i,value)
    uc.emu_start(symbols['fx_module_prepare_report'],returned,count=10000000)
    if uc.reg_read(UC_ARM64_REG_PC)!=returned:raise ValueError('compiled preflight did not return')
    result=uc.reg_read(UC_ARM64_REG_X0)
    reason,_,expected,actual=struct.unpack('<IIQQ',bytes(uc.mem_read(issue,24)))
    name=bytes(uc.mem_read(issue+24,64)).split(b'\0',1)[0].decode('ascii')
    if result and bytes(uc.mem_read(image,capacity))!=b'\xa5'*capacity:
        raise ValueError('failed compatibility check mutated the image')
    return dict(schema='fenestra.compiled-module-preflight/v1',target=profile['target'],result=result,
        issue=dict(reason=reason,binding=name,expected=expected,actual=actual),
        core_sha256=profile['core_sha256'],module_sha256=hashlib.sha256(package).hexdigest(),
        failed_image_unchanged=True if result else None,module_entry_executed=False,device_functions_mapped=False,
        image_executable=False,package_readonly=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('module',type=Path);p.add_argument('--build-dir',type=Path,required=True)
    args=p.parse_args()
    if args.module.stat().st_size>1048576:p.error('module exceeds package limit')
    print(json.dumps(check(args.build_dir,args.module.read_bytes()),indent=2))


if __name__=='__main__':main()
