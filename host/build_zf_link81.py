"""Reproduce the hardware-tested Zf C3.01 link81 EG from local original inputs.

Keep the tested base layout and apply four checked Zf corrections. No camera
connection, emulator, network access or package material is needed here.
GNU AArch64 GCC 13.3.0 is required for the pinned instruction layout.
"""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

from build_zf_core import ROOT
from build_zf_image import branch
from zf_c301 import BASE

BASE_CORE_SHA256 = 'bc723a5ffb226b7fc190f07c2557cad51512aa21defdeb13cceb9b8c054f5f6a'
BASE_EG_SHA256 = 'be779398bbdbb356b93f4d28d133433a19795f2394b1cebcfce44133a30a8820'
LINK81_EG_SHA256 = 'acfb28499112f73f75f31f82f2a442ea603e3e19affda54a0656204a13cdd7f8'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compile_adapter(source, output, address, entry, extra=()):
    elf = output / (source.stem + '.elf')
    code = output / (source.stem + '.bin')
    command = ['aarch64-linux-gnu-gcc', '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror',
               '-ffreestanding', '-fno-builtin', *extra, '-fno-pie', '-no-pie',
               '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector',
               '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-fstack-usage',
               '-nostdlib', '-Wl,--build-id=none', '-Wl,-Ttext=' + hex(address),
               '-Wl,-e,' + entry, '-I' + str(ROOT / 'include'),
               '-I' + str(ROOT / 'src/platform/zf_c301'), str(source), '-o', str(elf)]
    subprocess.run(command, check=True, timeout=45)
    subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary', '--only-section=.text',
                    str(elf), str(code)], check=True, timeout=15)
    symbols = {}
    for line in subprocess.check_output(['aarch64-linux-gnu-nm', '-n', str(elf)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    return code.read_bytes(), symbols[entry]


def apply_corrections(original, read_code, read_entry, directory_code):
    """Accept only the pinned base and checked adapter layout; return a CRC'd EG."""
    if sha(original) != BASE_EG_SHA256:
        raise ValueError('base EG differs from the tested link81 layout')
    if len(read_code) != 228 or read_entry != 0x401e76c8 or len(directory_code) != 120:
        raise ValueError('adapter layout differs; use GNU AArch64 GCC 13.3.0')
    image = bytearray(original)
    changes = []

    def replace(address, before, after, reason):
        at = address - BASE
        if not before or len(before) != len(after) or image[at:at+len(before)] != before:
            raise ValueError('patch preimage differs: ' + reason)
        image[at:at+len(after)] = after
        changes.append(dict(address=hex(address), bytes=len(after), reason=reason,
                            before_sha256=sha(before), after_sha256=sha(after)))

    # Module files identify the original EG payload, excluding its CRC.
    replace(0x401e1f0c, bytes.fromhex('61630091'), bytes.fromhex('61030291'), 'module-identity')
    replace(0x401e7600, bytes(len(read_code)), read_code, 'cached-card-read')
    for address in (0x401e5370, 0x401e6650):
        replace(address, struct.pack('<Q', 0x402e67ec), struct.pack('<Q', read_entry), 'card-read-pointer')
    native = original[0x402e67ec-BASE:0x402e67ec-BASE+16]
    replace(0x401e5378, native, read_code[read_entry-0x401e7600:read_entry-0x401e7600+16], 'card-read-preimage')
    # The <=65-byte startup reader already uses partial sectors; keep it intact.
    replace(0x401e7720, bytes(len(directory_code)), directory_code, 'directory-case')
    replace(0x401e1470, original[0x401e1470-BASE:0x401e1470-BASE+4],
            branch(0x401e1470, 0x401e7720), 'directory-next-entry')
    # Reuse the existing native viewport query for ordinary module pages too.
    replace(0x401a3830, struct.pack('<I', 0x35000cc2), struct.pack('<I', 0x14000066), 'visible-viewport')
    image[-2:] = binascii.crc_hqx(image[:-2], 0).to_bytes(2, 'big')
    if sha(image) != LINK81_EG_SHA256:
        raise ValueError('final EG differs from hardware-tested link81')
    return bytes(image), changes


def build(application, output):
    output = Path(output).resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('choose a fresh output directory below build/')
    output.mkdir(parents=True)
    baseline = output / 'baseline'
    base_eg = output / 'baseline.eg.bin'
    command = [sys.executable, '-B', str(ROOT / 'host/build_zf_image.py'),
               '--application', str(Path(application).resolve()), '--output-dir', str(baseline),
               '--candidate', str(base_eg), '--owner-worker', '--native-menu',
               '--ui-dispatch', '--native-ptp', '--module-executor']
    with (output / 'build.log').open('w', encoding='utf-8') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    if sha((baseline / 'core.bin').read_bytes()) != BASE_CORE_SHA256:
        raise ValueError('base core differs; use GNU AArch64 GCC 13.3.0')
    source = ROOT / 'src/platform/zf_c301'
    read, entry = compile_adapter(source / 'card_read.c', output, 0x401e7600,
                                  'fx_zf_card_read', ('-fno-tree-loop-distribute-patterns',))
    directory, directory_entry = compile_adapter(source / 'directory_case.c', output,
                                                 0x401e7720, 'fx_zf_directory_next')
    if directory_entry != 0x401e7720:
        raise ValueError('directory adapter entry differs')
    image, changes = apply_corrections(base_eg.read_bytes(), read, entry, directory)
    (output / 'link81.eg.bin').write_bytes(image)
    result = dict(schema='fenestra.zf-c301-link81/v1', target='zf-c301',
                  hardware_tested_reference='link81', eg_sha256=sha(image), bytes=len(image),
                  exact_reference_match=True, module_identity='original-eg-payload-sha256',
                  changes=changes)
    (output / 'link81.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True, help='original Zf C3.01 EG including CRC')
    parser.add_argument('--output-dir', type=Path, required=True, help='fresh directory below build/')
    args = parser.parse_args()
    print(json.dumps(build(args.application, args.output_dir), indent=2))


if __name__ == '__main__':
    main()
