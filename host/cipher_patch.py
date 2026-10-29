"""Fenestra ciphertext range patches. Standard library only; no device access."""
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import zipfile

SCHEMA = 'fenestra.ciphertext-patch/v1'
MAX_FIRMWARE = 256 * 1024 * 1024
MAX_MANIFEST = 2 * 1024 * 1024
MAX_RANGES = 8192

def sha(data):
    return hashlib.sha256(data).hexdigest()

def read_bounded(path, limit=MAX_FIRMWARE):
    with Path(path).open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('input exceeds supported size')
    return data

def atomic_new(path, data):
    """Publish a fully written file without replacing an existing destination."""
    path = Path(path)
    if path.exists() or path.is_symlink():
        raise FileExistsError('destination already exists')
    descriptor, temporary = tempfile.mkstemp(prefix='.fenestra-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        # link fails if the destination appeared concurrently. Same-filesystem
        # hard links are supported on NTFS and ordinary Linux filesystems.
        os.link(temporary, path)
    finally:
        os.unlink(temporary)

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate manifest key')
        result[key] = value
    return result

def digest_value(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
        raise ValueError('invalid SHA-256')
    return value

def make_patch(original, candidate, target, destination):
    if not 0 < len(original) == len(candidate) <= MAX_FIRMWARE:
        raise ValueError('fixed-size nonempty firmware required')
    if not re.fullmatch('[a-z0-9-]{1,48}', target):
        raise ValueError('invalid target')
    # Coalesce adjacent changed blocks, then trim unchanged endpoints. This
    # bounds memory/work for large images without a quadratic binary diff.
    intervals = []
    for start in range(0, len(original), 4096):
        end = min(start + 4096, len(original))
        if original[start:end] != candidate[start:end]:
            if intervals and intervals[-1][1] == start:
                intervals[-1][1] = end
            else:
                intervals.append([start, end])
    if not 1 <= len(intervals) <= MAX_RANGES:
        raise ValueError('empty or excessively fragmented patch')
    rows, payloads = [], []
    for index, (start, end) in enumerate(intervals):
        while original[start] == candidate[start]:
            start += 1
        while original[end - 1] == candidate[end - 1]:
            end -= 1
        old, new = original[start:end], candidate[start:end]
        member = f'ranges/{index:04d}.bin'
        rows.append(dict(offset=start, length=end-start, before_sha256=sha(old),
                         after_sha256=sha(new), member=member))
        payloads.append((member, new))
    manifest = dict(schema=SCHEMA, target=target, firmware_bytes=len(original),
                    original_sha256=sha(original), result_sha256=sha(candidate), ranges=rows)
    import io
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name, data in [('manifest.json', json.dumps(manifest, sort_keys=True, indent=2).encode()), *payloads]:
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    atomic_new(destination, stream.getvalue())
    return manifest

def apply_patch(original_path, patch_path, output_path):
    """Check the entire source before reading replacement bytes or creating output."""
    original = read_bounded(original_path)
    with zipfile.ZipFile(patch_path, 'r') as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if len(names) != len(set(names)) or not 2 <= len(names) <= MAX_RANGES + 1:
            raise ValueError('duplicate or invalid archive member count')
        if any(e.compress_type != zipfile.ZIP_STORED or e.flag_bits & 1 for e in entries):
            raise ValueError('only unencrypted, stored patch members are supported')
        if sum(e.file_size for e in entries) > MAX_FIRMWARE + MAX_MANIFEST:
            raise ValueError('patch exceeds supported size')
        info = archive.getinfo('manifest.json')
        if info.file_size > MAX_MANIFEST:
            raise ValueError('manifest too large')
        manifest = json.loads(archive.read(info), object_pairs_hook=unique_object)
        expected_keys = {'schema', 'target', 'firmware_bytes', 'original_sha256', 'result_sha256', 'ranges'}
        if not isinstance(manifest, dict) or set(manifest) != expected_keys or manifest['schema'] != SCHEMA:
            raise ValueError('unsupported patch manifest')
        if not isinstance(manifest['target'], str) or not re.fullmatch('[a-z0-9-]{1,48}', manifest['target']):
            raise ValueError('invalid target')
        if type(manifest['firmware_bytes']) is not int or manifest['firmware_bytes'] != len(original):
            raise ValueError('original firmware length mismatch')
        if digest_value(manifest['original_sha256']) != sha(original):
            raise ValueError('original firmware SHA-256 mismatch; no output written')
        digest_value(manifest['result_sha256'])
        rows = manifest['ranges']
        if not isinstance(rows, list) or not 1 <= len(rows) <= MAX_RANGES:
            raise ValueError('invalid replacement list')
        allowed, previous, result = {'manifest.json'}, 0, bytearray(original)
        for index, row in enumerate(rows):
            if not isinstance(row, dict) or set(row) != {'offset', 'length', 'before_sha256', 'after_sha256', 'member'}:
                raise ValueError('invalid replacement record')
            start, length = row['offset'], row['length']
            if type(start) is not int or type(length) is not int or start < previous or length <= 0 or start + length > len(original):
                raise ValueError('overlapping, unordered or out-of-bounds replacement')
            member = f'ranges/{index:04d}.bin'
            if row['member'] != member:
                raise ValueError('noncanonical payload member')
            entry = archive.getinfo(member)
            if entry.file_size != length:
                raise ValueError('replacement length mismatch')
            payload = archive.read(entry)
            if digest_value(row['before_sha256']) != sha(original[start:start+length]) or digest_value(row['after_sha256']) != sha(payload):
                raise ValueError('replacement checksum mismatch')
            result[start:start+length] = payload
            previous = start + length
            allowed.add(member)
        if set(names) != allowed or sha(result) != manifest['result_sha256']:
            raise ValueError('unexpected members or final firmware checksum mismatch')
    atomic_new(output_path, result)
    if sha(read_bounded(output_path)) != manifest['result_sha256']:
        raise OSError('output readback checksum mismatch')
    return manifest
