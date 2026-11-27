"""Build immutable multi-version replacement recipes from local original images."""
import hashlib
import json
from pathlib import Path
import struct

from cipher_patch import atomic_new

HEADER = struct.Struct('<4sIII')
VERSION = struct.Struct('<32s32s32sQIIII')
SEGMENT = struct.Struct('<4I')
MAX_U32 = (1 << 32) - 1


def integer(value, name):
    if isinstance(value, str):
        value = int(value, 0)
    if type(value) is not int or not 0 <= value <= (1 << 64) - 1:
        raise ValueError(f'{name} must fit an unsigned 64-bit offset')
    return value


def digest(value):
    if not isinstance(value, str) or len(value) != 64:
        raise ValueError('application_sha256 must contain 64 hexadecimal characters')
    return bytes.fromhex(value)


def pack(manifest, directory):
    if manifest.get('schema') != 'fenestra.range-substitution/v1':
        raise ValueError('unsupported recipe schema')
    versions = manifest.get('versions')
    if not isinstance(versions, list) or not versions:
        raise ValueError('at least one compatible version is required')
    directory = Path(directory)
    identities = set()
    records = []
    parts = []
    cursor = HEADER.size + len(versions) * VERSION.size
    for version in versions:
        identity = digest(version['application_sha256'])
        if identity in identities:
            raise ValueError('duplicate application identity')
        identities.add(identity)
        start = integer(version['region']['start'], 'region start')
        end = integer(version['region']['end'], 'region end')
        if start >= end or end - start > MAX_U32:
            raise ValueError('region must have 1..UINT32_MAX bytes')
        original_path = directory / version['original']
        with original_path.open('rb') as source:
            source.seek(0, 2)
            if end > source.tell():
                raise ValueError('region exceeds original image')
            source.seek(start)
            original = source.read(end - start)
        if len(original) != end - start:
            raise ValueError('short original image')
        original_hash = hashlib.sha256(original).digest()
        expected = version.get('original_sha256')
        if expected is not None and original_hash != digest(expected):
            raise ValueError('original region checksum differs')
        segments = version['segments']
        if not segments:
            raise ValueError('at least one replacement segment is required')
        table = []
        blobs = []
        previous = start
        data_offset = cursor + len(segments) * SEGMENT.size
        output_hash = hashlib.sha256()
        for segment in sorted(segments, key=lambda row: integer(row['start'], 'start')):
            first = integer(segment['start'], 'segment start')
            last = integer(segment['end'], 'segment end')
            if first < previous or first >= last or last > end:
                raise ValueError('segments overlap or exceed their original region')
            with (directory / segment['file']).open('rb') as payload:
                contents = payload.read(last - first + 1)
            if len(contents) != last - first:
                raise ValueError('replacement length must equal end - start')
            output_hash.update(memoryview(original)[previous-start:first-start])
            output_hash.update(contents)
            table.append(SEGMENT.pack(first-start, last-start, data_offset, 0))
            blobs.append(contents)
            data_offset += len(contents)
            previous = last
        output_hash.update(memoryview(original)[previous-start:])
        if data_offset > MAX_U32:
            raise ValueError('recipe exceeds format capacity')
        records.append(VERSION.pack(identity, original_hash, output_hash.digest(), start,
                                    end-start, len(segments), cursor, 0))
        parts.extend((*table, *blobs))
        cursor = data_offset
    return HEADER.pack(b'FNR1', 1, cursor, len(versions)) + b''.join(records + parts)


def write(manifest_path, output):
    manifest_path = Path(manifest_path).resolve(strict=True)
    data = pack(json.loads(manifest_path.read_text(encoding='utf-8')), manifest_path.parent)
    atomic_new(output, data)
    return dict(bytes=len(data), versions=HEADER.unpack_from(data)[3],
                sha256=hashlib.sha256(data).hexdigest())
