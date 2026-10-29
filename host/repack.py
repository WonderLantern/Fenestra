"""Offline, user-material-backed firmware reconstruction and patch production."""
import hashlib
from pathlib import Path
import subprocess
import sys
from cipher_patch import read_bounded, make_patch, sha
import zr_plain_container
import z9_update_codec as z9
import z9_update_envelope as z9_envelope
from platform_profiles import PROFILES

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = {
    'zr-c110': dict(size=117159984, sha256='43bf4f5f1278fa6b0a551ceb6fa4ee749dc74bbc8a5ef4fba19b30062184e546',
                    plaintext='5fa08a63b55508b578850b6e08b829c3dbaf3a9006e9c704dd771ef53f61548a', header=608, component='eg2210_0110k0.b'),
    'z9-c532': dict(size=101846611, sha256=z9.RAW_SHA256, plaintext=z9.PLAIN_SHA256, header=32, component=z9.MAIN_NAME),
}

def zr_transform(body, material):
    if len(material) != 48 or len(body) < 32:
        raise ValueError('ZR material must be AES-256 key followed by 16-byte initial counter (48 bytes)')
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    stream = Cipher(algorithms.AES(material[:32]), modes.CTR(material[32:])).encryptor()
    stream.update(bytes(32))
    return body[:32] + stream.update(body[32:]) + stream.finalize()

def zr_tail(digest):
    state, result = digest[0], bytearray()
    for index in range(32):
        state = (214013 * state + 2531011) & 0xffffffff
        if index >= 20:
            result.append((state >> 16) & 255)
    return bytes(result)

def decode_original(target, raw, key, suffix=None):
    profile = PRODUCTION[target]
    if len(raw) != profile['size'] or sha(raw) != profile['sha256']:
        raise ValueError('unsupported original package: complete SHA-256 differs')
    plain = zr_transform(raw[608:], key) if target == 'zr-c110' else z9.transform(raw[32:], key)
    if sha(plain) != profile['plaintext']:
        raise ValueError('key/material does not decode the pinned original body')
    rows = zr_plain_container.inspect(plain)['components'] if target == 'zr-c110' else z9.directory(plain)
    matches = [row for row in rows if row['name'] == profile['component']]
    if len(matches) != 1:
        raise ValueError('missing original application component')
    entry = matches[0]
    size = entry['payload_bytes'] if target == 'zr-c110' else entry['size'] - 2
    main = plain[entry['offset']:entry['offset']+size]
    if sha(main) != PROFILES[target]['sha256']:
        raise ValueError('original application component identity differs')
    if target == 'zr-c110':
        if suffix is None or len(suffix) != 8:
            raise ValueError('ZR requires an 8-byte user-supplied digest suffix')
        digest = hashlib.sha1(plain + suffix).digest()
        if raw[0x240:0x254] != digest or raw[0x254:0x260] != zr_tail(digest) or raw[0x220:0x240] != raw[:32][::-1]:
            raise ValueError('original ZR header/material consistency failed')
    else:
        suffix = z9_envelope.suffix_from_original(main)
        if not z9_envelope.verify(raw, plain, suffix):
            raise ValueError('original Z9 header consistency failed')
    return plain, main, suffix, entry

def rebuild(target, raw, plain, main, replacement, key, suffix, entry):
    if len(main) != len(replacement) or main == replacement:
        raise ValueError('application must contain fixed-size changes')
    if target == 'zr-c110':
        changed = bytearray(plain)
        changed[entry['offset']:entry['offset']+len(replacement)] = replacement
        body, _ = zr_plain_container.repair_crcs(bytes(changed))
        before = zr_plain_container.inspect(plain)['components']
        after = zr_plain_container.inspect(body)['components']
        if any(a != b for a, b in zip(before, after) if a['name'] != PRODUCTION[target]['component']):
            raise ValueError('unrelated component changed')
        header = bytearray(raw[:608])
        digest = hashlib.sha1(body + suffix).digest()
        header[0x240:0x254] = digest
        header[0x254:0x260] = zr_tail(digest)
        # Preserve the original key envelope because the original AES key/IV
        # remain unchanged and their full-body decoding was checked above.
        candidate = bytes(header) + zr_transform(body, key)
        if zr_transform(candidate[608:], key) != body:
            raise ValueError('ZR encryption round trip differs')
    else:
        body = z9.replace_main(plain, replacement)
        candidate = z9_envelope.header(raw, body, suffix) + z9.transform(body, key)
        if z9.transform(candidate[32:], key) != body or not z9_envelope.verify(candidate, body, suffix):
            raise ValueError('Z9 round trip/header differs')
    if len(candidate) != len(raw):
        raise ValueError('update package size changed')
    return candidate

def build_core(target, firmware, output):
    output = Path(output).resolve()
    if not output.is_relative_to(ROOT/'build') or output.exists():
        raise ValueError('choose a new directory below build/')
    if target == 'zr-c110':
        command = ['build_c110_menu.py', '--firmware', str(firmware), '--output-dir', str(output),
                   '--native-capture', '--sensor-trace', '--event-trace']
        candidate = output/'c110-menu.snapshot'
    else:
        command = ['build_z9_core.py', '--firmware', str(firmware), '--output-dir', str(output)]
        candidate = output/'z9-core.main'
    subprocess.run([sys.executable, '-B', str(ROOT/'host'/command[0]), *command[1:]], cwd=ROOT, check=True)
    subprocess.run([sys.executable, '-B', str(ROOT/'host/verify_platform_build.py'), '--target', target,
                    '--firmware', str(firmware), '--build', str(output), '--output', str(output/'verification.json')], cwd=ROOT, check=True)
    return candidate

def produce(target, firmware, key_file, suffix_file, work, patch, *, release='r3', envelope_key=None):
    work, patch = Path(work).resolve(), Path(patch).resolve()
    if release == 'r3' and target == 'zr-c110' and envelope_key is None:
        raise ValueError('ZR r3 requires --envelope-key (user RSA public/private PEM)')
    if not work.is_relative_to(ROOT/'build') or work.exists():
        raise ValueError('choose a new private work directory below build/')
    if patch.exists() or patch.is_symlink() or not patch.parent.is_dir():
        raise ValueError('patch destination must be new, with an existing parent')
    raw = read_bounded(firmware)
    # Reject a wrong original before opening key material or starting tools.
    p = PRODUCTION[target]
    if len(raw) != p['size'] or sha(raw) != p['sha256']:
        raise ValueError('original package SHA-256 mismatch')
    key = read_bounded(key_file, 48 if target == 'zr-c110' else z9.PERIOD)
    suffix = read_bounded(suffix_file, 8) if suffix_file else None
    plain, main, suffix, entry = decode_original(target, raw, key, suffix)
    work.mkdir(parents=True)
    original = work/'original-application.bin'
    original.write_bytes(main)
    candidate_file = build_core(target, original, work/'core')
    release_receipt = None
    if release == 'r3':
        from release_exact import normalize
        replacement, release_receipt = normalize(target, candidate_file)
    elif release == 'custom':
        replacement = read_bounded(candidate_file)
    else:
        raise ValueError('unknown release mode')
    candidate = rebuild(target, raw, plain, main, replacement, key, suffix, entry)
    if release == 'r3':
        from release_exact import finalize
        candidate = finalize(target, candidate, key, envelope_key)
        import json
        (work/'release-reproduction.json').write_text(json.dumps(release_receipt, indent=2)+'\n', encoding='utf-8')
    manifest = make_patch(raw, candidate, target, patch)
    return {k: manifest[k] for k in ('target', 'firmware_bytes', 'original_sha256', 'result_sha256')}
