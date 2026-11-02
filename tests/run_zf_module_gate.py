"""Bounded AArch64 fixture for the Zf arena and copied-service entry gate.

Uses an explicitly supplied local QEMU and an isolated virt machine with no
camera, storage or network. Every invocation requires a fresh output folder.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(output, qemu, simd=False):
    out = Path(output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    module = out / 'module'
    command = [sys.executable, str(ROOT/'host/build_sdk_module.py'),
               str(ROOT/'tests/zf_module_gate_module.c'), '--id', 'gate-zf',
               '--target', 'zf-c301', '--output-dir', str(module)]
    if simd:command.append('--simd')
    with (out/'module-build.log').open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=60)
    sources = ['tests/zf_module_gate_package.S','src/module.c','src/card.c',
               'src/sha256.c','tests/zf_module_gate_boot.S','tests/zf_module_gate_fixture.c',
               'src/platform/zf_c301/module_execution.c','src/platform/zf_c301/module_runtime.c',
               'src/platform/zf_c301/address_space.c','src/platform/zf_c301/exec_context.c',
               'src/platform/zf_c301/private_call.S','src/platform/zf_c301/user_call.S',
               'src/exec.c','src/runtime.c','src/memory.c','src/loader.c']
    command = ['aarch64-linux-gnu-gcc','-DFX_GATE_WLM=1',
               '-DFX_GATE_PACKAGE="'+str(module/'gate-zf.wlm')+'"',
               '-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
               '-ffreestanding','-fno-builtin','-fno-pie','-no-pie',
               '-mgeneral-regs-only','-mstrict-align','-mno-outline-atomics',
               '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
               '-ffunction-sections','-fdata-sections','-nostdlib',
               '-DFX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1',
               '-DFX_ZF_EXPERIMENTAL_MODULE_RUNTIME=1',
               '-DFX_ZF_EXPERIMENTAL_EXEC_CONTEXT=1',
               '-DFX_ZF_EXPERIMENTAL_ADDRESS_SPACE=1',
               '-Iinclude','-Isrc/platform/zf_c301',*sources,
               '-Wl,--gc-sections,-T,tests/zf_module_gate.ld','-o',str(out/'gate.elf')]
    (out/'build-command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (out/'build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,
                       check=True,timeout=60)
    command = [str(qemu),'-machine','virt','-cpu','cortex-a53','-m','128M',
               '-nographic','-monitor','none','-net','none',
               '-icount','shift=0,align=off,sleep=off',
               '-semihosting-config','enable=on,target=native','-kernel',str(out/'gate.elf')]
    (out/'run-command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (out/'run.log').open('w') as log:
        result = subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=20)
    message = (out/'run.log').read_text()
    passed = result.returncode==0 and 'PASS WLM card digest target loader arena' in message
    (out/'result.json').write_text(json.dumps({'passed':passed,'returncode':result.returncode,
        'scope':'isolated AArch64 Zf arena/entry/return/revoke','simd':simd,
        'timing':'instruction-count virtual clock; not a performance measurement',
        'camera_operations':0},indent=2)+'\n')
    print(message,end='')
    if not passed:
        raise RuntimeError('Zf module gate fixture failed')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--qemu',required=True,type=Path)
    parser.add_argument('--simd',action='store_true')
    args=parser.parse_args()
    run(args.output,args.qemu,args.simd)
