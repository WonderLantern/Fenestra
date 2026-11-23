"""Build a standalone card loader in a hash-verified original MAIN image."""
import json
import shutil
import struct
import subprocess

from card_payload import ADDRESS, CAPACITY
from cipher_patch import sha
from displaced import SITES
from platform_profiles import BASE, ROOT, load

LOADER = ADDRESS+CAPACITY
CONTROL = 0x401ff000
RESERVATION_END = 0x40200000
# app-ready event, shutdown event, drive query call, lookup, readiness
BOOT = {
    'zr-c110': (20, 11, 0x4090e1f0, 0x40913680, 0x40913620),
    'z9-c532': (19, 7, 0x408bb228, 0x408c0674, 0x408c0614),
}
NATIVE = {
    'DELAY': 'rtos.task.delay',
    'TASK_CREATE': 'rtos.task.create',
    'TASK_RESUME': 'rtos.task.resume',
    'ALLOCATE': 'rtos.memory.allocate',
    'FREE': 'rtos.memory.release',
    'OPEN': 'camera.card.open',
    'READ': 'camera.card.read',
    'CLOSE': 'camera.card.close',
    'MAP': 'rtos.mmu.map',
    'UNMAP': 'rtos.mmu.unmap',
    'ATTRIBUTES': 'rtos.mmu.attributes',
    'INVALIDATE_ALL': 'rtos.mmu.invalidate-all',
}


def branch(site, destination, link=False):
    delta = destination-site
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('out-of-range AArch64 branch')
    return struct.pack('<I', (0x94000000 if link else 0x14000000) | ((delta//4) & 0x3ffffff))


def native_inputs(target, profile, original, out):
    ready, shutdown, probe, lookup, status = BOOT[target]
    [site] = [address for symbol, address in SITES[target].items() if symbol.endswith('_system_original')]
    first, = struct.unpack_from('<I', original, site-BASE)
    if (any(original[ADDRESS-BASE:RESERVATION_END-BASE]) or first != 0xa9bc63f9
        or original[probe-BASE:probe-BASE+4] != branch(probe, lookup, True)
        or original[probe+16-BASE:probe+20-BASE] != branch(probe+16, status, True)):
        raise ValueError('original application startup or reservation differs')
    fields = {key: profile['functions'][name] for key,name in NATIVE.items()}
    fields.update(APP_READY=ready, SHUTDOWN=shutdown, DRIVE_INDEX=lookup, DRIVE_STATUS=status,
                  MMU_CONTEXT=profile['tables']['rtos.mmu.context'][0])
    identity = ','.join(str(byte) for byte in bytes.fromhex(profile['sha256']))
    macros = ''.join(f'#define FX_BOOT_{name} {value:#x}\n' for name,value in fields.items())
    (out/'card_boot_profile.h').write_text(
        macros+f'#define FX_BOOT_TARGET "{target.upper()}"\n'
        f'static const uint8_t fx_boot_application_sha[32]={{{identity}}};\n', encoding='ascii')
    trampoline = out/'boot_trampoline.S'
    trampoline.write_text(
        '.section .text.card_boot_entry,"ax",%progbits\n.global fx_card_boot_entry\n'
        'fx_card_boot_entry:\nb fx_card_boot_dispatch\n'
        '.text\n.global fx_card_boot_original\nfx_card_boot_original:\n'
        f'.inst {first:#x}\nb fx_card_boot_continue\n', encoding='ascii')
    return site, trampoline


def run(*command):
    return subprocess.run(command, cwd=ROOT, check=True, timeout=120, text=True,
                          stdout=subprocess.PIPE).stdout


def link_loader(out, trampoline, continuation):
    cc, ld, objcopy, nm = (shutil.which(name) for name in ('clang','ld.lld','llvm-objcopy','llvm-nm'))
    if not all((cc,ld,objcopy,nm)):
        raise RuntimeError('clang, ld.lld, llvm-objcopy and llvm-nm are required')
    sources = [ROOT/'src'/name for name in ('card_boot_entry.c','card_boot_native.c','card_payload.c','memory.c')]
    objects = []
    for source in (*sources,trampoline):
        obj = out/(source.stem+'.o')
        flags = ['-std=c11','-Oz','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                 '-DFX_FREESTANDING=1','-ffreestanding','-fno-builtin','-mgeneral-regs-only',
                 '-mstrict-align','-fno-stack-protector','-fno-asynchronous-unwind-tables',
                 '-ffunction-sections','-fdata-sections'] if source.suffix == '.c' else []
        run(cc,'--target=aarch64-none-elf',*flags,'-I',ROOT/'include','-I',out,'-c',source,'-o',obj)
        objects.append(obj)
    script = out/'bootstrap.ld'
    script.write_text(f'''ENTRY(fx_card_boot_entry)
SECTIONS {{
  . = {LOADER:#x};
  .text : {{ KEEP(*(.text.card_boot_entry)) *(.text*) }}
  .rodata : {{ *(.rodata*) }}
  .data : {{ *(.data*) }}
  .bss : {{ *(.bss*) *(COMMON) }}
  ASSERT(. <= {CONTROL:#x}, "loader exceeds its reservation")
  ASSERT(SIZEOF(.data) + SIZEOF(.bss) == 0, "loader needs initialized storage")
  /DISCARD/ : {{ *(.comment) *(.eh_frame*) *(.note*) }}
}}
''', encoding='ascii')
    elf, binary = out/'bootstrap.elf', out/'bootstrap.bin'
    run(ld,'-m','aarch64elf','-T',script,'--no-undefined','--gc-sections',
        f'--defsym=fx_card_boot_continue={continuation:#x}',*objects,'-o',elf)
    run(objcopy,'-O','binary',elf,binary)
    [entry] = [int(line.split()[0],16) for line in run(nm,'--defined-only',elf).splitlines()
               if line.endswith(' fx_card_boot_entry')]
    image = binary.read_bytes()
    if entry != LOADER or not 0 < len(image) <= CONTROL-LOADER:
        raise ValueError('invalid loader address or image size')
    return image


def build(target, firmware, output, *, emit_main=False):
    profile, original = load(target, firmware)
    out = output.resolve()
    if not out.is_relative_to(ROOT/'build') or out == ROOT/'build':
        raise ValueError('choose a new output directory beneath build/')
    out.mkdir(parents=True)
    site, trampoline = native_inputs(target, profile, original, out)
    loader = link_loader(out, trampoline, site+4)
    offset, start = site-BASE, LOADER-BASE
    instruction = branch(site, LOADER)
    candidate = bytearray(original)
    candidate[start:start+len(loader)] = loader
    candidate[offset:offset+4] = instruction
    report = dict(target=target, loader_abi=1, boot_file='FENESTRA/BOOT.BIN',
                  original_sha256=profile['sha256'], candidate_sha256=sha(candidate),
                  loader_address=LOADER, loader_bytes=len(loader), loader_sha256=sha(loader),
                  branch_site=site, branch_word=instruction.hex(), flashable=False)
    if emit_main:
        (out/'bootstrap.main').write_bytes(candidate)
    (out/'build.json').write_text(json.dumps(report,indent=2)+'\n', encoding='ascii')
    return report
