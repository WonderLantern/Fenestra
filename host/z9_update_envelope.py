"""C5.32 outer digest, independently reconstructed from its original verifier.

All ROM inputs are read from the hash-pinned original MAIN, never bundled.
The 12 remaining header bytes are preserved, not claimed as authenticated.
"""
import hashlib
import hmac
from platform_profiles import BASE, PROFILES


def suffix_from_original(main):
    if hashlib.sha256(main).hexdigest() != PROFILES['z9-c532']['sha256']:
        raise ValueError('pinned original Z9 C5.32 MAIN required')
    # 0x4102c13c supplies 0x450 and "Z_9"; 0x41197dd4 uses this ROM
    # seed instead of its first argument. 0x41197e48 complements/reverses
    # 0x450 before the original transform at 0x41197b50.
    seed = main[0x43df3290-BASE:0x43df3290-BASE+8]
    model_index = 0
    for c in b'Z_9':
        model_index = (10 * model_index + (c-48 if 48 <= c <= 57 else c % 10)) & 0xffffffff
    table_a = main[0x43df318e-BASE:0x43df318e-BASE+256]
    table_b = main[0x43df308e-BASE:0x43df308e-BASE+256]
    key_bytes = ((~0x450) & 0xffffffff).to_bytes(4, 'little')
    key_index = key_bytes[0] ^ key_bytes[1] ^ key_bytes[2] ^ key_bytes[3]
    step = table_a[model_index & 255]
    current, increment = table_b[key_index], step * 0x260
    result = bytearray()
    for byte in seed:
        current = (current + increment) & 0xffffffff
        increment = (increment + step) & 0xffffffff
        result.append(byte ^ (current & 255))
    return bytes(result)


def digest(decoded, encoded_body_prefix, suffix):
    if len(decoded) < 32 or len(encoded_body_prefix) != 32 or len(suffix) != 8:
        raise ValueError('body prefix and suffix sizes differ')
    # The native decoder intentionally skips its first 32 bytes. Hash the
    # runtime representation, not the third-party fully decoded twin.
    state = hashlib.sha1(encoded_body_prefix)
    state.update(memoryview(decoded)[32:])
    state.update(suffix)
    return state.digest()


def header(original, decoded, suffix):
    if len(original) != len(decoded) + 32:
        raise ValueError('fixed-layout original size differs')
    return digest(decoded, original[32:64], suffix) + original[20:32]


def verify(envelope, decoded, suffix):
    return len(envelope) == len(decoded) + 32 and hmac.compare_digest(
        envelope[:20], digest(decoded, envelope[32:64], suffix))
