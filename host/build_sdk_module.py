"""Compile an independent AArch64 .wlm; never rebuild or replace demo."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
from pack_module import pack
from target_c110 import PACKAGE_SHA256, require_module_target
from platform_profiles import PROFILES
from module_header import validate_package

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--id', required=True)
    p.add_argument('--source',dest='support',type=Path,action='append',default=[],help='additional owned C translation unit')
    p.add_argument('--output-dir', type=Path, required=True)
    p.add_argument('--required', type=lambda x: int(x, 0), default=0x3f0000)
    p.add_argument('--optimize',choices=('2','s','z'),default='2',help='LLVM optimization level; s/z reduce card package size')
    p.add_argument('--simd',action='store_true',help='enable module FP/Advanced SIMD; requires the new core capability')
    p.add_argument('--optional', type=lambda x: int(x, 0), default=0)
    p.add_argument('--native-stack-bytes', type=lambda x: int(x,0),
                   help='v3: owned RTOS task stack, 32768..1048576 bytes, page aligned')
    p.add_argument('--user-stack-bytes', type=lambda x: int(x,0),
                   help='v3: EL0 module stack, 4096..1048576 bytes, page aligned')
    p.add_argument('--target',choices=tuple(PROFILES),default='zr-c110')
    args = p.parse_args()
    if args.simd:args.required|=0x400000
    source, out = args.source.resolve(strict=True), args.output_dir.resolve()
    if not source.is_relative_to(ROOT) or not out.is_relative_to(ROOT) or out == ROOT:
        raise ValueError('owned source and output required')
    support=[f.resolve(strict=True) for f in args.support]
    if any(not f.is_relative_to(ROOT) or f.suffix!='.c' for f in support) or len(set([source,*support]))!=1+len(support):
        raise ValueError('distinct owned C support sources required')
    # Validate names/limits before creating files.
    row = dict(id=args.id, file=args.id + '.wlm', sha256='00' * 32, bytes=129,
               required=args.required, optional=args.optional, enabled=True)
    if (not args.id or len(args.id)>15 or
        any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789_-' for c in args.id) or
        not 0<=args.required<=0x7f0000 or args.required&~0x7f0000 or args.optional):
        raise ValueError('invalid module identity/capabilities')
    clang, linker = shutil.which('clang'), shutil.which('ld.lld')
    if not clang or not linker:
        raise RuntimeError('LLVM required')
    out.mkdir(parents=True, exist_ok=False)
    obj, elf = out / 'module.o', out / 'module.elf'
    compile_flags=[clang, '--target=aarch64-none-elf', '-std=c11', '-O'+args.optimize, '-Wall', '-Wextra', '-Werror',
                    '-Wconversion', '-Wshadow', '-ffreestanding', '-fno-builtin', *([] if args.simd else ['-mgeneral-regs-only']),
                    '-mstrict-align', '-fno-stack-protector', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
                    '-fpie','-ffunction-sections','-fdata-sections','-DFX_FREESTANDING=1','-I', str(ROOT / 'include')]
    objects=[];dependencies=set()
    for i,path in enumerate([source,*support,ROOT/'src/memory.c']):
        destination=obj if i==0 else out/f'support-{i}.o';objects.append(destination)
        dep=out/f'support-{i}.d'
        subprocess.run([*compile_flags,'-MMD','-MF',str(dep),'-MT','fx_module','-c',str(path),'-o',str(destination)],check=True,timeout=30)
        # Pin actual transitive includes, including owned headers outside the
        # entry's directory and included C fixtures. -MMD excludes toolchain
        # system headers. The fixed target avoids drive-letter colon parsing.
        body=dep.read_text(encoding='utf-8').replace('\\\n','').removeprefix('fx_module:')
        for token in re.findall(r'(?:\\.|[^\s])+',body):
            name=re.sub(r'\\([ #:\\])',r'\1',token)
            dependency=Path(name).resolve(strict=True)
            if not dependency.is_relative_to(ROOT):raise ValueError('module dependency outside owned project')
            dependencies.add(dependency)
    subprocess.run([linker, '-m', 'aarch64elf', '-pie', '--no-dynamic-linker', '-Bsymbolic','--gc-sections',
                    '-z', 'max-page-size=4096', '-e', 'fenestra_module_entry',*map(str,objects),'-o',str(elf)], check=True, timeout=30)
    identity=PROFILES[args.target]['module_identity']
    data = pack(elf.read_bytes(), bytes.fromhex(identity), args.required,entry_abi=1,
                native_stack_bytes=args.native_stack_bytes,user_stack_bytes=args.user_stack_bytes)
    if args.target=='zr-c110':require_module_target(data)
    elif data[24:56]!=bytes.fromhex(identity):raise ValueError('module target identity differs')
    preflight=validate_package(data,bytes.fromhex(identity))
    row.update(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))
    (out / row['file']).write_bytes(data)
    # -MMD already records every actual owned transitive include. Unrelated
    # profile/UI headers are not inputs to this module and must not invalidate
    # its compiled source identity merely because another adapter changed.
    sources = [source,*support,*sorted(dependencies),
               ROOT / 'host/build_sdk_module.py', ROOT / 'host/pack_module.py',
               ROOT / 'host/target_c110.py',ROOT/'host/module_header.py',ROOT/'host/platform_profiles.py',ROOT/'src/memory.c']
    report = dict(modules=[row], target=args.target, preflight=preflight, simd=args.simd, optimize=args.optimize,
                  sources={str(f.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(f.read_bytes()).hexdigest() for f in sources})
    (out / 'module.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
