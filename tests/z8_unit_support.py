"""Shared build/run support for the Z8 unit fixtures (isolated virt machine).

Locates clang/ld.lld on PATH and QEMU via FX_QEMU or the default z8-qemu build;
tests skip cleanly when either is unavailable (CI without a Linux toolchain
leg, or developer machines without the local emulator tree).
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

C_SOURCES = ['src/memory.c']
S_FLAGS = ['--target=aarch64-none-elf', '-Iinclude', '-Isrc/platform/z8_c311']
C_FLAGS = ['--target=aarch64-none-elf', '-std=c11', '-O2', '-Wall', '-Wextra',
           '-Werror', '-Wconversion', '-Wshadow', '-ffreestanding', '-fno-builtin',
           '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector',
           '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
           '-ffunction-sections', '-fdata-sections', '-Iinclude',
           '-Isrc/platform/z8_c311']


def find_tools():
    clang, lld = shutil.which('clang'), shutil.which('ld.lld')
    qemu = os.environ.get('FX_QEMU') or str(
        ROOT.parent / 'z8-qemu' / '.local' / 'build' / 'qemu-system-aarch64')
    if not (clang and lld and Path(qemu).is_file()):
        return None
    return clang, lld, qemu


def build(workdir, fixture, platform_sources, name, ld='tests/z8_unit_fixture.ld'):
    clang, lld, _ = find_tools()
    objects = []
    sources = ['tests/z8_unit_boot.S', fixture, *platform_sources, *C_SOURCES]
    for src in sources:
        obj = workdir / (src.replace('/', '__') + '.o')
        command = [clang, *(S_FLAGS if src.endswith('.S') else C_FLAGS),
                   '-c', src, '-o', str(obj)]
        subprocess.run(command, cwd=ROOT, check=True, timeout=120)
        objects.append(str(obj))
    elf = workdir / (name + '.elf')
    subprocess.run([lld, '-m', 'aarch64elf', '-T', ld,
                    '--gc-sections', *objects, '-o', str(elf)],
                   cwd=ROOT, check=True, timeout=120)
    return elf


def run(qemu, workdir, elf):
    rom = workdir / 'reset.bin'
    rom.write_bytes(struct.pack('<IIQ', 0x58000040, 0xd61f0000, 0x401a0000))
    command = [qemu, '-machine', 'virt', '-cpu', 'cortex-a53', '-m', '128M',
               '-bios', str(rom), '-nographic', '-monitor', 'none', '-net', 'none',
               '-icount', 'shift=0,align=off,sleep=off',
               '-semihosting-config', 'enable=on,target=native',
               '-device', 'loader,file=' + str(elf) + ',cpu-num=0']
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            timeout=120)
    return result.returncode, result.stdout + result.stderr


def build_and_run(fixture, platform_sources, pass_line, ld='tests/z8_unit_fixture.ld'):
    """Returns None (skipped) or (returncode, output, passed)."""
    tools = find_tools()
    if tools is None:
        return None
    clang, lld, qemu = tools
    with tempfile.TemporaryDirectory(prefix='z8-unit-') as tmp:
        workdir = Path(tmp)
        elf = build(workdir, fixture, platform_sources, 'unit', ld=ld)
        rc, output = run(qemu, workdir, elf)
        return rc, output, (rc == 0 and pass_line in output)
