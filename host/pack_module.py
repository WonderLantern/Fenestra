"""Pack a freestanding AArch64 PIE into the bounded Fenestra module format."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

HEADER = 128
MEMORY_LIMIT = 4 * 1024 * 1024
FILE_LIMIT = 1024 * 1024


def pack(elf: bytes, firmware: bytes, capabilities: int = 0, *, entry_abi: int = 0,
         native_stack_bytes=None, user_stack_bytes=None):
    if type(entry_abi) is not int or entry_abi not in (0,1):raise ValueError('unsupported module entry ABI')
    explicit_stacks = native_stack_bytes is not None or user_stack_bytes is not None
    if explicit_stacks:
        native_stack_bytes = 65536 if native_stack_bytes is None else native_stack_bytes
        user_stack_bytes = 65536 if user_stack_bytes is None else user_stack_bytes
        if (entry_abi != 1 or type(native_stack_bytes) is not int or type(user_stack_bytes) is not int
                or not 32768 <= native_stack_bytes <= 1048576 or not 4096 <= user_stack_bytes <= 1048576
                or (native_stack_bytes | user_stack_bytes) & 4095):
            raise ValueError('explicit service stacks must be page aligned and within the native contract')
    if len(firmware) != 32 or not 0 <= capabilities <= 0xffffffffffffffff:
        raise ValueError("invalid target or capability mask")
    if len(elf) < 64 or elf[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("expected little-endian ELF64")
    (kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize,
     phcount, shsize, shcount, shnames) = struct.unpack_from("<HHIQQQIHHHHHH", elf, 16)
    if kind != 3 or machine != 183 or version != 1 or ehsize != 64 or phsize != 56 or shsize != 64:
        raise ValueError("expected freestanding AArch64 ET_DYN")
    if phcount > 32 or shcount > 256 or phoff + phcount * 56 > len(elf) or shoff + shcount * 64 > len(elf):
        raise ValueError("invalid ELF tables")
    segments = []
    for i in range(phcount):
        ptype, pflags, offset, address, physical, filesz, memsz, alignment = struct.unpack_from(
            "<IIQQQQQQ", elf, phoff + i * 56)
        if ptype in (3, 7):
            raise ValueError("dynamic interpreter/TLS not supported")
        if ptype == 2:
            if offset + filesz > len(elf) or filesz % 16:
                raise ValueError("invalid dynamic table")
            for position in range(offset, offset + filesz, 16):
                tag, value = struct.unpack_from("<QQ", elf, position)
                if tag == 1:
                    raise ValueError("shared-library dependencies not supported")
        if ptype != 1 or memsz == 0:
            continue
        if (not pflags & 4 or pflags & ~7 or pflags & 3 == 3 or filesz > memsz or
            offset + filesz > len(elf) or address + memsz > MEMORY_LIMIT or alignment == 0 or
            alignment > 4096 or alignment & (alignment - 1) or (address - offset) % alignment):
            raise ValueError("invalid load segment; use a 4096-byte maximum page size")
        permissions = (1 if pflags & 4 else 0) | (2 if pflags & 2 else 0) | (4 if pflags & 1 else 0)
        # ELF p_align constrains the load base and address/file-offset congruence.
        # A segment itself may start partway into its page.
        field_alignment = alignment if address == 0 else min(alignment, address & -address)
        segments.append((address, elf[offset:offset + filesz], memsz, permissions, field_alignment))
    if not 1 <= len(segments) <= 8 or min(s[0] for s in segments) != 0:
        raise ValueError("unsupported segment count/base")
    for i, a in enumerate(segments):
        for b in segments[:i]:
            if a[0] < b[0] + b[2] and b[0] < a[0] + a[2]:
                raise ValueError("overlapping segments")
    if entry % 4 or not any(s[3] & 4 and s[0] <= entry and entry + 4 <= s[0] + len(s[1]) for s in segments):
        raise ValueError("entry is not in file-backed executable text")
    relocations = []
    for i in range(shcount):
        name, stype, sflags, address, offset, size, link, info, align, entsize = struct.unpack_from(
            "<IIQQQQIIQQ", elf, shoff + i * 64)
        if stype != 4 or not sflags & 2:
            continue
        if entsize != 24 or size % 24 or offset + size > len(elf):
            raise ValueError("invalid RELA table")
        for position in range(offset, offset + size, 24):
            target, rinfo, addend = struct.unpack_from("<QQq", elf, position)
            if rinfo != 1027 or addend < 0:  # R_AARCH64_RELATIVE, symbol index zero
                raise ValueError("only internal R_AARCH64_RELATIVE relocations are supported")
            if target % 8 or not any(s[3] & 2 and s[0] <= target and target + 8 <= s[0] + s[2] for s in segments):
                raise ValueError("relocation does not belong to writable module data")
            if not any(s[0] <= addend < s[0] + s[2] for s in segments):
                raise ValueError("relocation leaves module memory")
            if any(old[0] == target for old in relocations):
                raise ValueError("duplicate relocation")
            relocations.append((target, 1, addend))
    if len(relocations) > 128:
        raise ValueError("too many relocations")
    table_end = HEADER + len(segments) * 24 + len(relocations) * 16
    position = table_end
    table = bytearray()
    payload = bytearray()
    for address, data, memsz, permissions, alignment in segments:
        table += struct.pack("<6I", address, position, len(data), memsz, permissions, alignment)
        payload += data
        position += len(data)
    body = bytes(table) + b"".join(struct.pack("<IIQ", *r) for r in relocations) + bytes(payload)
    header = bytearray(HEADER)
    header[:8] = b"FENESTRA"
    struct.pack_into("<4I", header, 8, 3 if explicit_stacks else 2 if entry_abi else 1, HEADER, 1, 1)
    if entry_abi:struct.pack_into('<I',header,116,entry_abi)
    if explicit_stacks:struct.pack_into('<2I',header,120,native_stack_bytes,user_stack_bytes)
    header[24:56] = firmware
    header[56:88] = hashlib.sha256(body).digest()
    struct.pack_into("<5IQ", header, 88, HEADER + len(body), max(s[0] + s[2] for s in segments),
                     entry, len(segments), len(relocations), capabilities)
    result = bytes(header) + body
    if len(result) > FILE_LIMIT:
        raise ValueError("module package exceeds 1 MiB")
    return result


def main():
    from target_c110 import load_profile, PACKAGE_SHA256, require_module_target
    load_profile()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--firmware-sha256", required=True)
    parser.add_argument("--capabilities", default="0")
    parser.add_argument('--entry-abi', choices=('legacy','services'), default='legacy')
    parser.add_argument('--native-stack-bytes', type=lambda x: int(x,0))
    parser.add_argument('--user-stack-bytes', type=lambda x: int(x,0))
    args = parser.parse_args()
    if args.firmware_sha256 != PACKAGE_SHA256:
        parser.error("current module build target is fixed to ZR C1.10")
    try:
        value = pack(args.elf.read_bytes(), bytes.fromhex(args.firmware_sha256), int(args.capabilities, 0),
                     entry_abi=int(args.entry_abi=='services'), native_stack_bytes=args.native_stack_bytes,
                     user_stack_bytes=args.user_stack_bytes)
    except (OSError, ValueError, struct.error) as exc:
        parser.error(str(exc))
    require_module_target(value)
    args.output.write_bytes(value)
    print(json.dumps({"path": str(args.output), "bytes": len(value),
                      "sha256": hashlib.sha256(value).hexdigest(), "camera_executed": False}))


if __name__ == "__main__":
    main()
