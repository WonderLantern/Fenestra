"""Z9 C5.32 fixed-layout update body codec; outer authentication is separate.

No external code, XOR tables, device metadata or firmware is bundled. A mask
is recovered locally from hash-pinned ciphertext/plaintext twins and checked
over their entire 24-bit-period stream before use. It is not an auth key.
"""
import binascii
import hashlib
import struct

RAW_SHA256 = '2f13e75d38cd5b65da5b1f21a7828bc8a65ab17dc4c1b435b040ee62d2ed1d81'
PLAIN_SHA256 = '448728b37cfb078bfe8787f85615e8cd173bab589efdee8e3e299e6f8113d7ba'
MAIN_NAME = 'eg1990_053200.bi'
PERIOD = 1 << 24


def sha(data):
    return hashlib.sha256(data).hexdigest()


def directory(plain):
    if len(plain) < 80:
        raise ValueError('truncated update body')
    count, header, reserved0, reserved1 = struct.unpack_from('>4I', plain, 32)
    if not 1 <= count <= 64 or header != 48 + count * 32 or reserved0 or reserved1:
        raise ValueError('unsupported directory header')
    if header > len(plain) - 16 or any(plain[-14:]):
        raise ValueError('invalid footer or directory extent')
    entries, end, names = [], header, set()
    for i in range(count):
        name, offset, size, r0, r1 = struct.unpack_from('>16s4I', plain, 48 + 32 * i)
        stem = name.rstrip(b'\0')
        if not stem or b'\0' in stem or any(c < 33 or c > 126 for c in stem) or b'/' in stem or b'\\' in stem:
            raise ValueError('invalid component name')
        name = stem.decode('ascii')
        if name in names or r0 or r1 or offset != end or size < 2 or offset + size > len(plain) - 16:
            raise ValueError('invalid component bounds/name/reserved fields')
        if binascii.crc_hqx(plain[offset:offset + size], 0):
            raise ValueError('component CRC mismatch: ' + name)
        entries.append(dict(name=name, offset=offset, size=size,
                            payload_sha256=sha(plain[offset:offset + size - 2])))
        names.add(name)
        end = offset + size
    if end != len(plain) - 16 or binascii.crc_hqx(plain[:end + 2], 0xcd18):
        raise ValueError('package CRC/extent mismatch')
    return entries


def xor_bytes(a, b):
    if len(a) != len(b):
        raise ValueError('XOR sizes differ')
    return (int.from_bytes(a, 'little') ^ int.from_bytes(b, 'little')).to_bytes(len(a), 'little')


def transform(data, mask):
    if len(mask) != PERIOD:
        raise ValueError('24-bit mask required')
    result = bytearray(len(data))
    for start in range(0, len(data), 1 << 20):
        size = min(1 << 20, len(data) - start)
        at = start % PERIOD
        result[start:start + size] = xor_bytes(data[start:start + size], mask[at:at + size])
    return bytes(result)


def original_twins(raw, plain):
    if len(raw) != 101846611 or sha(raw) != RAW_SHA256 or sha(plain) != PLAIN_SHA256:
        raise ValueError('pinned official C5.32 twins required')
    if len(raw) != len(plain) + 32:
        raise ValueError('outer prefix size differs')
    entries = directory(plain)
    if len(entries) != 6 or entries[2]['name'] != MAIN_NAME:
        raise ValueError('C5.32 component layout differs')
    mask = xor_bytes(raw[32:32 + PERIOD], plain[:PERIOD])
    if transform(plain, mask) != raw[32:]:
        raise ValueError('ciphertext/plaintext stream relationship differs')
    return mask, entries


def replace_main(plain, main):
    entries = directory(plain)
    matches = [e for e in entries if e['name'] == MAIN_NAME]
    if len(matches) != 1 or matches[0]['size'] != len(main) + 2:
        raise ValueError('fixed-size C5.32 MAIN required')
    entry = matches[0]
    result = bytearray(plain)
    start, size = entry['offset'], entry['size']
    result[start:start + size - 2] = main
    struct.pack_into('>H', result, start + size - 2, binascii.crc_hqx(main, 0))
    struct.pack_into('>H', result, len(result) - 16, binascii.crc_hqx(result[:-16], 0xcd18))
    result = bytes(result)
    updated = directory(result)
    for old, new in zip(entries, updated):
        if old['name'] != MAIN_NAME and old != new:
            raise ValueError('non-MAIN component changed')
    if result[:entries[0]['offset']] != plain[:entries[0]['offset']]:
        raise ValueError('directory changed')
    return result
