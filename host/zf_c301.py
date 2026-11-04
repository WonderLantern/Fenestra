"""Read-only Zf C3.01 input checks and bounded Z9 hook comparison.

Input checks only. The integrated build and package entry points are in
build_zf_link81.py and zf_c301_repack.py.
"""
import binascii
import hashlib
import hmac
import os
import struct
from pathlib import Path

from zr_plain_container import valid_name

PACKAGE_SIZE = 107820261
PACKAGE_SHA256 = 'bfc1ef4ec1588ef10c85e06bf1bf5bcaa7456e49bb2b7160dd4043ab8934d1a6'
BODY_SIZE = 107819653
BODY_SHA256 = 'e9e009c1fd41a3ecc70e78b044c68860072a5c14543e80e57ef1615d386a1200'
EG_SIZE = 79144343
EG_SHA256 = '0b53631396314e17e5b5a46a6758a35ae521c1e8bddc2fb59b61bcf72e4889f8'
EG_PAYLOAD_SHA256 = '6891edd22d9a40042e4718480d29082682bbd12aea172c51f4f164b97f74fe08'
BASE = 0x40160000
BODY_OFFSET = 0x260
EG_NAME = 'eg2110_030100.bi'
Z9_CODE_START = 0x40000
Z9_CODE_END = 0xa0000
SYSCTL_TABLE = 0x43d6b160
SYSCTL_DISPATCH = 0x40d64e7c
SYSCTL_EVENTS = {7: 'StartTask2nd', 8: 'BSShutdown', 19: 'AppFirstStartCompleteSyncHook',
                 20: 'AppFirstStartComplete', 24: 'CardRemove', 25: 'CardMountComplete'}
NATIVE_PAGES = dict(first=0x48684b8, stop=0x6c6, kinds={2: 1433, 0: 3, 3: 128, 5: 169})


def inspect_body(body):
    """Validate Zf's contiguous inner directory and CRCs, without authentication."""
    data = memoryview(body)
    if len(data) < 224 or data[:32] != b' ' * 32:
        raise ValueError('invalid Zf plaintext prefix or length')
    count, header, reserved0, reserved1 = struct.unpack_from('>4I', data, 32)
    if (count, header, reserved0, reserved1) != (5, 208, 0, 0):
        raise ValueError('invalid Zf directory geometry')
    previous, seen, rows = header, set(), []
    for i in range(count):
        label, offset, size, r0, r1 = struct.unpack_from('>16s4I', data, 48 + 32 * i)
        name = label.rstrip(b'\0')
        valid_name(name, seen)
        if label != name.ljust(16, b'\0') or r0 or r1 or offset != previous or size < 2 or offset + size > len(data) - 16:
            raise ValueError('invalid Zf component bounds or reserved fields')
        content = data[offset:offset + size]
        if binascii.crc_hqx(content, 0):
            raise ValueError('Zf component CRC-16 failed')
        rows.append(dict(name=name.decode('ascii'), plaintext_body_offset=offset,
                         encrypted_package_offset=BODY_OFFSET + offset,
                         plaintext_bytes_with_crc=size, plaintext_sha256_with_crc=hashlib.sha256(content).hexdigest(),
                         plaintext_payload_bytes=size - 2, plaintext_payload_sha256=hashlib.sha256(content[:-2]).hexdigest()))
        previous = offset + size
    if previous != len(data) - 16 or data[-14:] != bytes(14) or binascii.crc_hqx(data[:-14], 0):
        raise ValueError('Zf body CRC-16, extent or trailer failed')
    return rows


def _checked_file(path, size, digest):
    with Path(path).open('rb') as f:
        if os.fstat(f.fileno()).st_size != size:
            raise ValueError('unsupported original file: length differs')
        sha = hashlib.sha256()
        length = 0
        header_digest = bytearray()
        while chunk := f.read(1024 * 1024):
            sha.update(chunk)
            start, end = max(0, 0x240 - length), min(len(chunk), 0x254 - length)
            if start < end:
                header_digest.extend(chunk[start:end])
            length += len(chunk)
        if length != size or sha.hexdigest() != digest or len(header_digest) != 20:
            raise ValueError('unsupported original file: length or SHA-256 differs')
        return bytes(header_digest)


def _compare_hooks(z9, zf, hooks):
    """Exact-window candidates only: no claim about function identity or ABI."""
    rows = []
    for name, address in hooks.items():
        at = address - BASE
        if at < 0 or at + 64 > len(z9):
            raise ValueError('Z9 hook outside original application')
        sizes = []
        for size in (16, 32, 64):
            window = z9[at:at + size]
            count = aligned = 0
            examples = []
            start = 0
            while (found := zf.find(window, start)) != -1:
                count += 1
                if (BASE + found) % 4 == 0:
                    aligned += 1
                    if len(examples) < 8:
                        examples.append(hex(BASE + found))
                start = found + 1
            sizes.append(dict(bytes=size, sha256=hashlib.sha256(window).hexdigest(), matches=count,
                              aligned_matches=aligned, aligned_examples=examples))
        rows.append(dict(hook=name, z9_address=hex(address), windows=sizes))
    return rows


def _inspect_sysctl_events(eg, table):
    """Check six original table records; a table name is not a hook contract."""
    events = []
    for number, suffix in SYSCTL_EVENTS.items():
        at = table - BASE + number * 88
        if at < 0 or at + 88 > len(eg):
            raise ValueError('Zf SYSCTL table outside application')
        identity, pointer = struct.unpack_from('<I4xQ', eg, at)
        name = ('eSyscCommandId_' + suffix).encode('ascii') + bytes(1)
        name_at = pointer - BASE
        if (identity != number or name_at < 0 or name_at + len(name) > len(eg)
                or eg[name_at:name_at + len(name)] != name):
            raise ValueError('Zf SYSCTL event identity differs')
        events.append(dict(id=number, name=suffix, record_sha256=hashlib.sha256(eg[at:at + 88]).hexdigest()))
    return dict(table_address=hex(table), row_bytes=88, events=events, dispatch_hook_verified=False)


def _inspect_dispatch_candidate(eg, address):
    at = address - BASE
    if at < 0 or at + 268 > len(eg):
        raise ValueError('Zf SYSCTL candidate outside application')
    return dict(address=hex(address), observed_span_bytes=268,
                span_sha256=hashlib.sha256(eg[at:at + 268]).hexdigest(),
                prefix_16_sha256=hashlib.sha256(eg[at:at + 16]).hexdigest(),
                complete_function_and_hook_verified=False)


def _inspect_native_pages(eg):
    from platform_profiles import verify_page_namespace
    result = verify_page_namespace({'native_pages': NATIVE_PAGES}, eg)
    result['scanned_array_does_not_overlap_extension_ids'] = result.pop('collision_free')
    result['scope'] = 'original group-2 array; runtime ownership and other groups not verified'
    return result


def _z9_comparison(path, zf):
    from platform_profiles import PROFILES
    from build_z9_core import HOOKS
    profile = PROFILES['z9-c532']
    if Path(path).stat().st_size not in (profile['bytes'], profile['bytes'] + 2):
        raise ValueError('expected the pinned Z9 C5.32 application')
    z9 = Path(path).read_bytes()
    if len(z9) == profile['bytes'] + 2:
        if binascii.crc_hqx(z9, 0):
            raise ValueError('Z9 application CRC-16 failed')
        z9 = z9[:-2]
    if len(z9) != profile['bytes'] or hashlib.sha256(z9).hexdigest() != profile['sha256']:
        raise ValueError('expected the pinned Z9 C5.32 application')
    names = ('fx_z9_menu_page', 'fx_z9_menu_text', 'fx_z9_system_dispatch', 'fx_z9_menu_navigate',
             'fx_z9_ptp_lookup', 'fx_z9_ptp_session', 'fx_z9_ptp_operations', 'fx_z9_rtos_printf')
    hooks = {name: address for address, name in HOOKS.items() if name in names}
    if len(hooks) != len(names):
        raise ValueError('Z9 base hook inventory differs')
    return dict(z9_application_sha256=profile['sha256'], windows=_compare_hooks(z9, zf, hooks),
                conclusion='Candidates only: matching bytes do not establish a function, ABI, lifecycle or safe hook.')


def inspect_inputs(firmware, body_file, application=None, k8_file=None, z9_application=None):
    expected_digest = _checked_file(firmware, PACKAGE_SIZE, PACKAGE_SHA256)
    if Path(body_file).stat().st_size != BODY_SIZE:
        raise ValueError('recovered original Zf body length differs')
    body = Path(body_file).read_bytes()
    if len(body) != BODY_SIZE or hashlib.sha256(body).hexdigest() != BODY_SHA256:
        raise ValueError('recovered original Zf body identity differs')
    rows = inspect_body(body)
    eg = next((row for row in rows if row['name'] == EG_NAME), None)
    if (eg is None or eg['plaintext_bytes_with_crc'] != EG_SIZE or eg['plaintext_sha256_with_crc'] != EG_SHA256
            or eg['plaintext_payload_sha256'] != EG_PAYLOAD_SHA256):
        raise ValueError('original Zf application identity differs')
    zf = body[eg['plaintext_body_offset']:eg['plaintext_body_offset'] + eg['plaintext_payload_bytes']]
    sysctl = _inspect_sysctl_events(zf, SYSCTL_TABLE)
    dispatch = _inspect_dispatch_candidate(zf, SYSCTL_DISPATCH)
    native_pages = _inspect_native_pages(zf)
    start, end = eg['plaintext_body_offset'] + Z9_CODE_START, eg['plaintext_body_offset'] + Z9_CODE_END
    if Z9_CODE_START < 0 or Z9_CODE_END > eg['plaintext_payload_bytes'] or start >= end:
        raise ValueError('comparison region outside Zf application')
    region = memoryview(body)[start:end]
    code_region = dict(module_offsets=[Z9_CODE_START, Z9_CODE_END],
                       analysis_addresses=[hex(BASE + Z9_CODE_START), hex(BASE + Z9_CODE_END)],
                       bytes=len(region), sha256=hashlib.sha256(region).hexdigest(),
                       all_zero=not any(region), loaded_or_executable_verified=False, ownership_verified=False)
    app_variant = None
    if application is not None:
        if Path(application).stat().st_size not in (EG_SIZE, EG_SIZE - 2):
            raise ValueError('original Zf application length differs')
        app = Path(application).read_bytes()
        app_hash = hashlib.sha256(app).hexdigest()
        if len(app) == EG_SIZE and app_hash == EG_SHA256:
            app_variant = 'complete module with CRC'
        elif len(app) == EG_SIZE - 2 and app_hash == EG_PAYLOAD_SHA256:
            app_variant = 'application payload without CRC'
        else:
            raise ValueError('original Zf application identity differs')
    digest_verified = None
    if k8_file is not None:
        with Path(k8_file).open('rb') as f:
            key = f.read(9)
        if len(key) != 8:
            raise ValueError('private digest suffix must be exactly eight bytes')
        digest = hashlib.sha1()
        digest.update(body)
        digest.update(key)
        if not hmac.compare_digest(digest.digest(), expected_digest):
            raise ValueError('original package SHA-1 does not match body and suffix')
        digest_verified = True
    result = dict(target='zf-c301', status='original-input-inspection-only',
                  original_update_sha256=PACKAGE_SHA256, plaintext_body_sha256=BODY_SHA256,
                  original_update_body_offset=BODY_OFFSET,
                  original_body_crc16_verified=True, original_header_sha1_verified=digest_verified,
                  application=eg, supplied_application=app_variant, components=rows,
                  analysis_base=hex(BASE), sysctl_events=sysctl, sysctl_dispatch_candidate=dispatch,
                  native_pages=native_pages,
                  z9_code_region_in_zf=code_region,
                  native_bindings_verified=False, runtime_build_available=False,
                  update_reconstruction_available=False, device_tested=False)
    if z9_application is not None:
        result['z9_hook_comparison'] = _z9_comparison(z9_application, zf)
    return result
