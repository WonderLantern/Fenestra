"""Plan an experimental Zf C3.01 core placement without patching firmware."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import struct

from zf_c301 import BASE, EG_SIZE, EG_SHA256, EG_PAYLOAD_SHA256

ROOT = Path(__file__).resolve().parents[1]
COPY_ENDPOINT = 0x41147D80
HOOK_SITE = 0x41147DE4
RESUME_SITE = 0x41147DE8
CAVE_VA = 0x40960000
CAVE_BYTES = 0x10000
MAPPED_CODE_VA = 0x401A0000
MAPPED_CODE_BYTES = 0x60000
MAPPED_CODE_MODULE_OFFSET = MAPPED_CODE_VA - BASE
COPY_ORIGINAL = (0xD287DE08, 0xF2A811A8)
COPY_EXTENDED = (0xD2800008, 0xF2A812E8)
HOOK_ORIGINAL = 0xD28B9013
HOOK_BRANCH = 0x17E06087


def identity(path):
    path = Path(path)
    h = hashlib.sha256()
    with path.open('rb') as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=h.hexdigest())


def validate_core_elf(path, image_limit, fixed_address=False):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('expected little-endian ELF64')
    fields = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
    kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phcount, shsize, shcount, shnames = fields
    expected_kind = 2 if fixed_address else 3
    if (kind, machine, version, ehsize, phsize) != (expected_kind, 183, 1, 64, 56):
        raise ValueError('expected AArch64 '+('ET_EXEC' if fixed_address else 'ET_DYN')+' core ELF')
    if phcount > 32 or phoff + phcount * phsize > len(data):
        raise ValueError('invalid ELF program headers')
    segments = []
    for i in range(phcount):
        ptype, pflags, offset, address, physical, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize)
        if ptype != 1 or not memsz:
            continue
        if offset + filesz > len(data) or filesz > memsz or pflags & ~7 or pflags & 3 == 3:
            raise ValueError('invalid core load segment')
        segments.append(dict(address=address, file_bytes=filesz, memory_bytes=memsz, flags=pflags, alignment=align))
    if not segments:
        raise ValueError('core has no loadable segments')
    base = min(row['address'] for row in segments)
    if fixed_address:
        if base != MAPPED_CODE_VA:
            raise ValueError('fixed core must start at the selected Zf mapped address')
        image = max(row['address'] + row['memory_bytes'] for row in segments) - base
        entry = entry - base
    else:
        if base != 0:
            raise ValueError('core must be a PIE with image base zero')
        image = max(row['address'] + row['memory_bytes'] for row in segments)
        entry = entry
    if image > image_limit:
        raise ValueError('core image exceeds the selected Zf code region')
    if entry < 0 or entry >= image or entry & 3:
        raise ValueError('core entry outside checked image')
    return dict(elf=identity(path), format='ET_EXEC' if fixed_address else 'ET_DYN', base=hex(base), entry_offset=entry, image_bytes=image, segments=segments,
                writable_static_data=any(row['flags'] & 2 for row in segments),
                runtime_linked=False)


def plan(application, output, core_elf=None, placement='cave', fixed_address=False):
    output = Path(output).resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('choose a new output directory below build/')
    application = Path(application)
    if application.stat().st_size not in (EG_SIZE, EG_SIZE - 2):
        raise ValueError('expected the pinned original Zf C3.01 EG')
    image = application.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if len(image) == EG_SIZE:
        if digest != EG_SHA256 or binascii.crc_hqx(image, 0):
            raise ValueError('complete Zf EG identity or CRC differs')
        payload = image[:-2]
    elif digest != EG_PAYLOAD_SHA256:
        raise ValueError('Zf EG payload identity differs')
    else:
        payload = image
    copy_at = COPY_ENDPOINT - BASE
    hook_at = HOOK_SITE - BASE
    if struct.unpack_from('<II', payload, copy_at) != COPY_ORIGINAL:
        raise ValueError('Zf original copy endpoint differs')
    if struct.unpack_from('<I', payload, hook_at)[0] != HOOK_ORIGINAL:
        raise ValueError('Zf original hook word differs')
    if placement not in ('cave', 'mapped-region'):
        raise ValueError('unknown Zf core placement')
    if placement == 'cave':
        placement_va, placement_bytes, source_offset = CAVE_VA, CAVE_BYTES, CAVE_VA - BASE
    else:
        placement_va, placement_bytes, source_offset = MAPPED_CODE_VA, MAPPED_CODE_BYTES, MAPPED_CODE_MODULE_OFFSET
    if payload[source_offset:source_offset + placement_bytes] != bytes(placement_bytes):
        raise ValueError('selected Zf core region is not all zero')
    displacement = placement_va - HOOK_SITE
    if displacement & 3 or not -(1 << 27) <= displacement < (1 << 27):
        raise ValueError('selected Zf region is outside AArch64 B range')
    branch_word = 0x14000000 | ((displacement >> 2) & 0x03ffffff)
    core = validate_core_elf(core_elf, placement_bytes, fixed_address) if core_elf else None
    output.mkdir(parents=True)
    result = dict(schema='fenestra.zf-c301-core-plan/v1', target='zf-c301',
                  application=identity(application), application_payload_sha256=hashlib.sha256(payload).hexdigest(),
                  copy_endpoint=dict(required=(placement == 'cave'), address=hex(COPY_ENDPOINT), original_words=[hex(x) for x in COPY_ORIGINAL],
                                     extended_words=[hex(x) for x in COPY_EXTENDED], extended_copy_end='0x40970000'),
                  hook=dict(address=hex(HOOK_SITE), original_word=hex(HOOK_ORIGINAL), branch_word=hex(branch_word),
                            resume=hex(RESUME_SITE)),
                  placement=dict(kind=placement, address=hex(placement_va), bytes=placement_bytes,
                                 module_offset=hex(source_offset), all_zero=True,
                                 qemu_mapping_verified=(placement == 'mapped-region'),
                                 runtime_owner_verified=False, hardware_cache_verified=False),
                  core_elf=core, runtime_build_available=False, hook_installed=False,
                  firmware_output_generated=False, installation_ready=False)
    (output / 'core-plan.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--core-elf', type=Path)
    parser.add_argument('--placement', choices=('cave', 'mapped-region'), default='cave')
    parser.add_argument('--fixed-address-core', action='store_true', help='accept a fixed-address ET_EXEC core at the mapped-region base')
    args = parser.parse_args()
    print(json.dumps(plan(args.application, args.output_dir, args.core_elf, args.placement, args.fixed_address_core), indent=2))


if __name__ == '__main__':
    main()
