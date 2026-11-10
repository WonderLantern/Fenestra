"""Isolated production Zf session execution; no firmware, disks or network."""
import argparse
import json
from pathlib import Path
import subprocess
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]

def run(output,qemu,simd=False):
    out=Path(output).resolve();out.mkdir(parents=True,exist_ok=False)
    module=out/'module'
    command=[sys.executable,str(ROOT/'host/build_sdk_module.py'),str(ROOT/'tests/zf_module_gate_module.c'),
             '--id','gate-zf','--target','zf-c301','--output-dir',str(module)]
    if simd:command.append('--simd')
    with (out/'module-build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
    sources=['tests/zf_module_gate_boot.S','tests/zf_module_gate_package.S','tests/zf_session_release.S',
        'tests/zf_session_fixture.c','src/platform/zf_c301/module_session.c',
        'src/platform/zf_c301/module_stage.c','src/platform/zf_c301/card.c',
        'src/platform/zf_c301/address_space.c','src/platform/zf_c301/exec_context.c',
        'src/platform/zf_c301/private_call.S','src/platform/zf_c301/user_call.S',
        'src/module.c','src/card.c','src/sha256.c','src/exec.c','src/runtime.c','src/memory.c',
        'src/loader.c','src/file_card.c','src/module_load_buffer.c','src/accounting.c','src/panic.c']
    command=['aarch64-linux-gnu-gcc','-DFX_GATE_PACKAGE="'+str(module/'gate-zf.wlm')+'"',
        '-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-ffreestanding','-fno-builtin',
        '-fno-pie','-no-pie','-mgeneral-regs-only','-mstrict-align','-mno-outline-atomics',
        '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
        '-ffunction-sections','-fdata-sections','-nostdlib','-DFX_ZF_EXPERIMENTAL_EXEC_CONTEXT=1',
        '-DFX_ZF_EXPERIMENTAL_ADDRESS_SPACE=1','-Iinclude','-Isrc/platform/zf_c301',*sources,
        '-Wl,--gc-sections,-T,tests/zf_session_fixture.ld','-o',str(out/'session.elf')]
    (out/'build-command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (out/'build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
    # Supplying a reset ROM enables virt's ACPI GED memory aperture. The ROM
    # only branches to our ELF; no guest firmware or operating system is used.
    rom=out/'reset.bin';rom.write_bytes(struct.pack('<IIQ',0x58000040,0xd61f0000,0x401a0000))
    command=[str(qemu),'-machine','virt,acpi=on','-cpu','cortex-a53','-m','128M,slots=1,maxmem=64G',
        '-object','memory-backend-ram,id=session,size=128M','-device','pc-dimm,memdev=session,addr=0xa00000000',
        '-bios',str(rom),
        '-nographic','-monitor','none','-net','none','-icount','shift=0,align=off,sleep=off',
        '-semihosting-config','enable=on,target=native',
        '-device','loader,file='+str(out/'session.elf')+',cpu-num=0']
    (out/'run-command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (out/'run.log').open('w') as log:
        result=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=20)
    message=(out/'run.log').read_text();print(message,end='')
    passed=result.returncode==0 and 'PASS production Zf session' in message
    (out/'result.json').write_text(json.dumps(dict(passed=passed,returncode=result.returncode,simd=simd,
        scope='production session/card/loader/private arena/EL0/context restore',camera_operations=0),indent=2)+'\n')
    if not passed:raise RuntimeError('session fixture failed')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--qemu',type=Path,required=True);p.add_argument('--simd',action='store_true');a=p.parse_args()
    run(a.output,a.qemu,a.simd)
