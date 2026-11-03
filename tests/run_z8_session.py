"""Isolated production Z8 C3.11 session execution; no firmware, disks or network.

Requires LLVM (clang + ld.lld; source ../tools/env.sh) and an explicitly
supplied QEMU binary (z8-qemu/.local/build/qemu-system-aarch64). Every
invocation requires a fresh --output folder. Builds the real hello WLM with
the Z8 module identity, compiles the freestanding fixture that links the
production session/stage/card sources (firmware entry points served by
address-placed branch stubs), runs the positive session and a corrupted-
package control that must FAIL, proving the load path verifies bytes.

Machine fidelity: -cpu cortex-a53 matches the z8-qemu nikon-z8-lab machine's
default CPU (the z9 lab uses the same), and the pc-dimm at 0xa00000000
mirrors the Z8 work-RAM window used by the production emulation.
"""
import argparse
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SOURCES = ['tests/z8_session_boot.S', 'tests/z8_session_package.S', 'tests/z8_session_fixture.c',
           'src/platform/z8_c311/module_session.c', 'src/platform/z8_c311/module_stage.c',
           'src/platform/z8_c311/card.c', 'src/platform/z8_c311/address_space.c',
           'src/platform/z8_c311/exec_context.c', 'src/platform/z8_c311/private_call.S',
           'src/platform/z8_c311/user_call.S', 'src/module.c', 'src/card.c', 'src/sha256.c',
           'src/exec.c', 'src/runtime.c', 'src/memory.c', 'src/loader.c', 'src/file_card.c',
           'src/module_load_buffer.c', 'src/accounting.c', 'src/panic.c']


def build(out, corrupt=False):
    # Mirror the production build shape (host/build_z8_core.py): C sources are
    # compiled with -mgeneral-regs-only so no FP/NEON reaches EL1 (the camera
    # OS environment does not guarantee CPACR FPEN for the core), while
    # assembly sources compile with bare flags so their SIMD save/restore
    # sequences pass clang's integrated assembler. Files are compiled
    # individually and linked with ld.lld, exactly like the core builder.
    clang, lld = shutil.which('clang'), shutil.which('ld.lld')
    if not clang or not lld:
        raise RuntimeError('LLVM (clang + ld.lld) required on PATH; source tools/env.sh')
    # The SDK builder only accepts repository-owned output directories; build/
    # is gitignored and every invocation uses a fresh folder below it.
    module = ROOT / 'build' / ('z8-session-' + out.name) / 'module'
    if not corrupt:
        # The WLM is target-identical for both builds; create it once (the SDK
        # builder only accepts fresh output directories).
        command = [sys.executable, str(ROOT / 'host/build_sdk_module.py'),
                   str(ROOT / 'examples/hello/module.c'), '--id', 'hello',
                   '--target', 'z8-c311', '--output-dir', str(module)]
        with (out / 'module-build.log').open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=120)
    c_flags = ['--target=aarch64-none-elf', '-std=c11', '-O2', '-Wall', '-Wextra',
               '-Werror', '-Wconversion', '-Wshadow', '-ffreestanding', '-fno-builtin',
               '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector',
               '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
               '-ffunction-sections', '-fdata-sections', '-Iinclude',
               '-Isrc/platform/z8_c311']
    s_flags = ['--target=aarch64-none-elf', '-Iinclude', '-Isrc/platform/z8_c311']
    objects = []
    for name in SOURCES:
        obj = out / (name.replace('/', '__') + ('.corrupt.o' if corrupt else '.o'))
        defines = []
        if name.endswith('z8_session_package.S'):
            defines = ['-DFX_GATE_PACKAGE="' + str(module / 'hello.wlm') + '"']
        elif corrupt and name.endswith('z8_session_fixture.c'):
            defines = ['-DFX_SESSION_CORRUPT=1']
        command = [clang, *(s_flags if name.endswith('.S') else c_flags), *defines,
                   '-c', name, '-o', str(obj)]
        with (obj.with_suffix('.log')).open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=120)
        objects.append(str(obj))
    name = 'session-corrupt' if corrupt else 'session'
    command = [lld, '-m', 'aarch64elf', '-T', 'tests/z8_session_fixture.ld',
               '--gc-sections', *objects, '-o', str(out / (name + '.elf'))]
    (out / (name + '-build-command.json')).write_text(json.dumps(command, indent=2) + '\n')
    with (out / (name + '-build.log')).open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=120)


def run(qemu, out, name):
    rom = out / 'reset.bin'
    # Reset ROM branches to the fixture entry; no guest firmware or OS is used.
    rom.write_bytes(struct.pack('<IIQ', 0x58000040, 0xd61f0000, 0x401a0000))
    command = [str(qemu), '-machine', 'virt,acpi=on', '-cpu', 'cortex-a53',
               '-m', '128M,slots=1,maxmem=64G',
               '-object', 'memory-backend-ram,id=session,size=128M',
               '-device', 'pc-dimm,memdev=session,addr=0xa00000000',
               '-bios', str(rom), '-nographic', '-monitor', 'none', '-net', 'none',
               '-icount', 'shift=0,align=off,sleep=off',
               '-semihosting-config', 'enable=on,target=native',
               '-device', 'loader,file=' + str(out / (name + '.elf')) + ',cpu-num=0']
    (out / (name + '-run-command.json')).write_text(json.dumps(command, indent=2) + '\n')
    with (out / (name + '-run.log')).open('w') as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log,
                                stderr=subprocess.STDOUT, timeout=120)
    return result.returncode, (out / (name + '-run.log')).read_text()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qemu', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    build(out, False)
    build(out, True)
    rc_ok, ok_log = run(args.qemu, out, 'session')
    rc_bad, bad_log = run(args.qemu, out, 'session-corrupt')
    passed = (rc_ok == 0 and 'PASS production Z8 session' in ok_log
              and rc_bad != 0 and 'load result 000000000000000a' in bad_log  # FX_INTEGRITY, 16-digit zero-pad
              and 'FAIL ' in bad_log and 'PASS' not in bad_log)
    (out / 'result.json').write_text(json.dumps(dict(
        passed=passed, positive_returncode=rc_ok, negative_returncode=rc_bad,
        scope='production session/card/loader/staging digest/private arena/EL0/context restore',
        camera_operations=0), indent=2) + '\n')
    print(ok_log, end='')
    print(bad_log[-160:], end='')
    if not passed:
        raise RuntimeError('session fixture failed')


if __name__ == '__main__':
    main()
