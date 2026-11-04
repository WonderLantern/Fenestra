"""Local C2.01 fixed-layout container and paired-CTR reconstruction.

All input bytes and optional authentication material come from local files.
No firmware/material is distributed. Container integrity is separate from
updater/hardware acceptance. Outputs remain DO_NOT_FLASH research artifacts.
"""
import binascii
import hashlib
import json
from pathlib import Path
import struct

RAW_SHA256 = '2369c01dcc194c1b9517e91ad2ee8bab8ff03cf52635473404ce8553b59ce131'
BODY_SHA256 = '3170ec4b3b5be263f24c0970f6ea487b77c6cfd7d68c07948bbf93bec409a2c8'
BODY_BYTES = 117640845
MAIN = 'eg2010_0201i0.bi'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def directory(body):
    if len(body) != BODY_BYTES or body[:32] != b' ' * 32 or body[-14:] != bytes(14):
        raise ValueError('wrong C2.01 container extent/prefix/trailer')
    if struct.unpack_from('>4I', body, 32) != (5, 208, 0, 0):
        raise ValueError('wrong C2.01 directory geometry')
    end = 208
    rows = []
    for index in range(5):
        raw_name, offset, size, reserved0, reserved1 = struct.unpack_from('>16s4I', body, 48 + 32 * index)
        name = raw_name.rstrip(b'\0').decode('ascii')
        if (not name or b'\0' in raw_name.rstrip(b'\0') or '/' in name or '\\' in name or
                reserved0 or reserved1 or offset != end or size < 2 or offset + size > len(body) - 16):
            raise ValueError('invalid component extent/name/reserved fields')
        member = memoryview(body)[offset:offset + size]
        if binascii.crc_hqx(member, 0):
            raise ValueError('component CRC differs: ' + name)
        rows.append(dict(name=name, offset=offset, bytes=size, sha256=sha(member)))
        end += size
    if end != len(body) - 16 or binascii.crc_hqx(memoryview(body)[:-14], 0):
        raise ValueError('whole C2.01 container CRC differs')
    return rows


def xor3(encoded, original, replacement):
    if len(encoded) != len(original) or len(original) != len(replacement):
        raise ValueError('paired CTR requires identical byte positions/length')
    # Fixed-size edits under an unchanged original AES-CTR key/counter envelope.
    # Bounded integer blocks avoid retaining an exported keystream artifact.
    result = bytearray()
    for at in range(0, len(encoded), 1 << 20):
        a, b, c = encoded[at:at + (1 << 20)], original[at:at + (1 << 20)], replacement[at:at + (1 << 20)]
        result.extend((int.from_bytes(a, 'little') ^ int.from_bytes(b, 'little') ^
                       int.from_bytes(c, 'little')).to_bytes(len(a), 'little'))
    return bytes(result)


def seal_header(raw, original, replacement, suffix):
    if len(raw)<608 or len(suffix)!=8:
        raise ValueError('complete header and eight local suffix bytes required')
    digest=hashlib.sha1(original+suffix).digest()
    if raw[0x240:0x254]!=digest or raw[0x220:0x240]!=raw[:32][::-1]:
        raise ValueError('authentication material does not validate original C2.01 envelope')
    # Keep the version/reserved tail, RSA slots and prefix byte-identical.
    # A recurrence fitting the original twelve bytes does not authorize edits.
    header=bytearray(raw[:608]);header[0x240:0x254]=hashlib.sha1(replacement+suffix).digest()
    return bytes(header)


def verify_application(original, candidate, receipt, profile):
    if len(candidate) != len(original) or sha(original) != profile['application_sha256']:
        raise ValueError('original/candidate C2.01 application extent differs')
    if sha(candidate) != receipt['candidate_sha256'] or receipt['target'] != 'z63-c201':
        raise ValueError('candidate build receipt differs')
    base = profile['image_base']
    begin, end = (profile['probe_code_region'][name] - base for name in ('begin', 'end'))
    if any(original[begin:end]):
        raise ValueError('original resident reservation is not empty')
    allowed = {profile['sysctl']['dispatcher']: 'fx_z63_system_dispatch'}
    for group, prefix in [('menu_hooks', 'fx_z63_menu_'), ('ptp_hooks', 'fx_z63_ptp_')]:
        allowed.update({row['address']: prefix + name for name, row in profile.get(group, {}).items()})
    calls={row['address']:row['symbol'] for row in profile.get('ui_calls',[])}
    allowed.update(calls)
    restored = bytearray(candidate)
    restored[begin:end] = original[begin:end]
    for address, symbol in receipt['hooks'].items():
        address = int(address, 16)
        if allowed.get(address) != symbol:
            raise ValueError('undeclared target hook')
        word = struct.unpack_from('<I', candidate, address - base)[0]
        delta = word & 0x3ffffff
        if delta & (1 << 25):
            delta -= 1 << 26
        target = address + delta * 4
        if word >> 26 != (0x25 if address in calls else 0x5) or target != receipt['symbols'][symbol] or not begin + base <= target < end + base:
            raise ValueError('hook does not branch to its resident symbol')
        restored[address - base:address - base + 4] = original[address - base:address - base + 4]
    if restored != original:
        raise ValueError('undeclared application change outside code/hooks')


def build(raw_path, body_path, candidate_path, receipt_path, output, suffix_path=None):
    raw, original = Path(raw_path).read_bytes(), Path(body_path).read_bytes()
    if len(raw) != BODY_BYTES + 608 or sha(raw) != RAW_SHA256 or sha(original) != BODY_SHA256:
        raise ValueError('pinned complete official C2.01 ciphertext/plaintext twins required')
    rows = directory(original)
    if raw[608:640] != original[:32]:
        raise ValueError('original clear body label differs')
    row = next(row for row in rows if row['name'] == MAIN)
    old_main = original[row['offset']:row['offset'] + row['bytes']]
    candidate = Path(candidate_path).read_bytes()
    receipt = json.loads(Path(receipt_path).read_text())
    profile = json.loads((Path(__file__).resolve().parents[1] / 'targets/z63-c201.json').read_text())
    verify_application(old_main, candidate, receipt, profile)
    changed = bytearray(original)
    start, size = row['offset'], row['bytes']
    changed[start:start + size - 2] = candidate[:-2]
    changed[start + size - 2:start + size] = binascii.crc_hqx(candidate[:-2], 0).to_bytes(2, 'big')
    changed[-16:-14] = binascii.crc_hqx(memoryview(changed)[:-16], 0).to_bytes(2, 'big')
    body = bytes(changed)
    after = directory(body)
    if any(before != updated for before, updated in zip(rows, after) if before['name'] != MAIN):
        raise ValueError('unrelated component changed')
    encoded = xor3(raw[608:], original, body)
    if xor3(raw[608:], original, encoded) != body:
        raise ValueError('paired CTR reconstruction differs')
    package = None
    if suffix_path is not None:
        suffix = Path(suffix_path).read_bytes()
        if len(suffix) != 8:
            raise ValueError('authentication suffix must be exactly eight local bytes')
        package = seal_header(raw,original,body,suffix) + encoded
        if hashlib.sha1(body + suffix).digest() != package[0x240:0x254]:
            raise ValueError('candidate authentication reconstruction differs')
    out = Path(output).resolve()
    if out.exists():
        raise ValueError('new output directory required')
    out.mkdir(parents=True)
    (out / 'PACKAGE.plain.DO_NOT_FLASH.bin').write_bytes(body)
    (out / 'PACKAGE.encoded-body.DO_NOT_FLASH.bin').write_bytes(encoded)
    if package is not None:
        (out / 'Z6_3_0201.DO_NOT_FLASH.bin').write_bytes(package)
    report = dict(schema='fenestra.z63-package/v1', target='z63-c201', original_sha256=RAW_SHA256,
        candidate_application_sha256=sha(candidate), packaged_application_sha256=after[2]['sha256'],
        body_sha256=sha(body), encoded_body_sha256=sha(encoded),
        package_sha256=sha(package) if package is not None else None,
        authentication_reconstructed=False, updater_tested=False, physical_camera_tested=False,
        package_digest_reconstructed=package is not None,outer_authentication_applied=False,
        original_header_outside_digest_preserved=True,
        do_not_flash=True, other_components_unchanged=True, original_application_reconstruction=True,
        components=after)
    (out / 'package.json').write_text(json.dumps(report, indent=2)+'\n')
    return report
