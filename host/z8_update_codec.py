"""Nikon Z8 C3.11 update-package codec (clean-room, facts-only).

Container layout (independently verified against the official package;
see Nikon_Z8_Firmware_Reverse_Engineering/scripts/verify_unpack.py V1-V8):

    file 0x0000  20-byte SHA-1(body || K8) authentication digest
                12-byte fixed outer field 5866cd124269c27e621da226
    file 0x0020  body starts; its first 32 bytes are a plaintext space label
    body 0x0020  header '>4I' = (5, 208, 0, 0)
    body 0x0030  module directory: 5 x '>16s4I' (name, start, size, r0, r1)
    body ...     five modules; each ends with a big-endian CRC16-HQX over the
                 preceding module bytes such that crc_hqx(module, 0) == 0
    body -0x10   package CRC16-HQX (big-endian) over body[:-16]
    body -0x0E   14 zero trailer bytes

The body is covered by a periodic 16 MiB XOR stream indexed by body offset:

    stream[i] = T1[i & 0xFF] ^ T2[(i >> 8) & 0xFF] ^ T3[(i >> 16) & 0xFF]

except the 32-byte label, which is never enciphered. The three 256-byte
tables are recoverable from any genuine package through two plaintext
anchors: a 64 KiB zero window at body offset 0xB10000 and an embedded
encrypted copy of T3 at body offset 0x3FF1F84.

K8 (8 bytes) is user-supplied key material; it is not embedded here.
"""
import binascii
import hashlib
import struct

RAW_BYTES = 99051117
RAW_SHA256 = 'd67413c2ede6b9ce8887175f20700bd9fbad2a973a4820968c45503d15d80d96'
OUTER_FIELD = bytes.fromhex('5866cd124269c27e621da226')
LABEL = b' ' * 32
PERIOD = 1 << 24
ZERO_WINDOW = 0xB10000
EMBEDDED_T3 = 0x3FF1F84
MAIN_NAME_PREFIX = b'eg2070_'


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def recover_tables(body_cipher):
    """Recover (T1, T2, T3) from the enciphered body of a genuine package."""
    if len(body_cipher) < EMBEDDED_T3 + PERIOD // 2:
        raise ValueError('package too short for format recovery')
    z = ZERO_WINDOW
    base = body_cipher[z]
    t1 = bytes(body_cipher[z + x] ^ base for x in range(256))
    t2 = bytes(body_cipher[z + 256 * y] ^ base for y in range(256))
    # The embedded table at EMBEDDED_T3 is T3 enciphered with T1/T2 already
    # applied; one known-zero stream position fixes the remaining constant.
    partial = bytes(body_cipher[EMBEDDED_T3 + x] ^ t1[(EMBEDDED_T3 + x) & 255]
                    ^ t2[((EMBEDDED_T3 + x) >> 8) & 255] for x in range(256))
    constant = partial[(z >> 16) & 255] ^ body_cipher[z]
    return t1, t2, bytes(v ^ constant for v in partial)


def xor_stream(body, tables):
    """body is the full plaintext body INCLUDING the leading label (left clear)."""
    t1, t2, t3 = tables
    out = bytearray(body[:32])
    n = len(body)
    # Stream index is body-relative (the leading 32-byte label stays clear and
    # the stream therefore starts offset by 32 at every period boundary).
    for i in range(32, n):
        out.append(body[i] ^ t1[i & 255] ^ t2[(i >> 8) & 255] ^ t3[(i >> 16) & 255])
    return bytes(out)


def xor_unstream(cipher_body, tables):
    """Inverse of xor_stream (cipher body with plaintext label → plaintext body)."""
    return xor_stream(cipher_body, tables)  # XOR is its own inverse


def decode(raw, auth_key, strict=True):
    """Verify a package and return (tables, plaintext body, main module).

    strict=True pins the complete original SHA-256 (generator input path);
    strict=False runs the structural checks only (rebuilt candidates).
    """
    if len(raw) != RAW_BYTES:
        raise ValueError('unexpected package size')
    if strict and _sha(raw) != RAW_SHA256:
        raise ValueError('original package SHA-256 mismatch')
    if raw[20:32] != OUTER_FIELD:
        raise ValueError('outer field differs')
    tables = recover_tables(raw[32:])
    body = xor_unstream(raw[32:], tables)
    if body[:32] != LABEL:
        raise ValueError('plaintext label mismatch')
    if body[-14:] != bytes(14):
        raise ValueError('trailer mismatch')
    if binascii.crc_hqx(body[:-14], 0) != 0:
        raise ValueError('package CRC mismatch')
    if hashlib.sha1(body + auth_key).digest() != raw[:20]:
        raise ValueError('authentication digest mismatch (K8 differs)')
    if struct.unpack_from('>4I', body, 32) != (5, 208, 0, 0):
        raise ValueError('module directory header differs')
    main = None
    for i in range(5):
        name, start, size, r0, r1 = struct.unpack_from('>16s4I', body, 48 + i * 32)
        if r0 or r1:
            raise ValueError('module reserved fields differ')
        module = body[start:start + size]
        if binascii.crc_hqx(module, 0) != 0:
            raise ValueError('module CRC mismatch: ' + name.rstrip(b'\0').decode('ascii'))
        if name.startswith(MAIN_NAME_PREFIX):
            main = (name, module)
    if main is None:
        raise ValueError('main application module not found')
    return tables, body, main[1], main[0]


def replace_main(body, tables, name, replacement):
    """Return a fresh plaintext body with the main module bytes replaced."""
    old_start = old_size = None
    for i in range(5):
        entry_name, start, size, r0, r1 = struct.unpack_from('>16s4I', body, 48 + i * 32)
        if entry_name.startswith(MAIN_NAME_PREFIX):
            if old_start is not None:
                raise ValueError('duplicate main module')
            old_start, old_size = start, size
    if old_start is None:
        raise ValueError('main application module not found')
    if len(replacement) != old_size:
        raise ValueError('replacement main must keep the original size')
    if binascii.crc_hqx(replacement, 0) != 0:
        raise ValueError('replacement main CRC residue nonzero')
    out = bytearray(body)
    out[old_start:old_start + old_size] = replacement
    out[112:128] = name.encode('ascii').ljust(16, b'\0')
    out[len(out) - 16:len(out) - 14] = binascii.crc_hqx(bytes(out[:-16]), 0).to_bytes(2, 'big')
    return bytes(out)


def encode(body, tables, auth_key):
    """Rebuild the encrypted package from a plaintext body."""
    candidate = hashlib.sha1(body + auth_key).digest() + OUTER_FIELD + xor_stream(body, tables)
    if len(candidate) != RAW_BYTES:
        raise ValueError('rebuilt package size differs')
    return candidate
