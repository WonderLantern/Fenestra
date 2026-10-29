"""Decode the entry contract in a .wlm header, independently of its name."""
import struct

def entry_abi(data):
    if len(data)<128 or data[:8]!=b'FENESTRA':raise ValueError('module header missing')
    version,header,abi,isa=struct.unpack_from('<4I',data,8)
    if (header,abi,isa)!=(128,1,1) or version not in (1,2,3):raise ValueError('unsupported module header')
    if version<3 and any(data[120 if version==2 else 116:128]):raise ValueError('reserved module header bytes set')
    if version>=2:
        kind=struct.unpack_from('<I',data,116)[0]
        if kind!=1:raise ValueError('unsupported module entry ABI')
        return kind
    return 1 if struct.unpack_from('<Q',data,108)[0]&0x3f0000 else 0


PACKAGE_LIMIT = 1024 * 1024
IMAGE_LIMIT = 4 * 1024 * 1024
STACK_DEFAULT = 65536
NATIVE_STACK_MIN = 32768
USER_STACK_MIN = 4096
STACK_LIMIT = 1024 * 1024


def stack_requirements(data):
    entry_abi(data)
    if struct.unpack_from('<I', data, 8)[0] < 3:
        return STACK_DEFAULT, STACK_DEFAULT
    native, user = struct.unpack_from('<2I', data, 120)
    if (not NATIVE_STACK_MIN <= native <= STACK_LIMIT or not USER_STACK_MIN <= user <= STACK_LIMIT
            or (native | user) & 4095):
        raise ValueError('invalid module stack declaration')
    return native, user


def validate_package(data, identity, *, capabilities=0x7f0000):
    """Independent worker preflight; never allocates or changes an image.

    Mirrors the Loader's descriptor/relocation contract and the actual native
    workers' capacities. The target identity is supplied by the chosen profile.
    This is corruption/ABI validation, not a package signature or sandbox.
    """
    if not 128 < len(data) <= PACKAGE_LIMIT:
        raise ValueError('module package exceeds native worker capacity')
    if entry_abi(data) != 1 or data[24:56] != identity:
        raise ValueError('module target or service entry differs')
    native_stack, user_stack = stack_requirements(data)
    import hashlib
    if hashlib.sha256(data[128:]).digest() != data[56:88]:
        raise ValueError('module body checksum differs')
    size, image, entry, count, relocs, required = struct.unpack_from('<5IQ', data, 88)
    if size != len(data) or not 0 < image <= IMAGE_LIMIT or required & ~capabilities:
        raise ValueError('module size/image/capabilities outside native worker contract')
    if not 0 < count <= 8 or relocs > 128:
        raise ValueError('module segment/relocation capacity exceeded')
    table_end = 128 + count * 24 + relocs * 16
    if table_end > len(data):
        raise ValueError('module descriptor tables truncated')
    segments = [struct.unpack_from('<6I', data, 128 + i * 24) for i in range(count)]
    def overlaps(a, n, b, m):
        return a < b + m and b < a + n
    def inside(at, n, permission, backed=False):
        return any(at >= start and n <= (file_bytes if backed else memory_bytes) - (at - start)
                   and flags & permission == permission
                   for start, _, file_bytes, memory_bytes, flags, _ in segments)
    for i, (start, offset, file_bytes, memory_bytes, flags, alignment) in enumerate(segments):
        if (not memory_bytes or file_bytes > memory_bytes or start > image or memory_bytes > image - start
                or offset < table_end or offset > size or file_bytes > size - offset
                or flags & ~7 or not flags & 1 or flags & 6 == 6
                or not 0 < alignment <= 4096 or alignment & (alignment - 1) or start & (alignment - 1)):
            raise ValueError('invalid module segment')
        for other, file_at, file_n, memory_n, other_flags, _ in segments[:i]:
            if overlaps(start, memory_bytes, other, memory_n) or (file_bytes and file_n and overlaps(offset, file_bytes, file_at, file_n)):
                raise ValueError('overlapping module segments')
            if flags != other_flags and overlaps(start & ~4095, ((start + memory_bytes + 4095) & ~4095) - (start & ~4095),
                                                  other & ~4095, ((other + memory_n + 4095) & ~4095) - (other & ~4095)):
                raise ValueError('incompatible permissions share a native page')
    if entry & 3 or not inside(entry, 4, 4, True):
        raise ValueError('module entry is not backed executable code')
    seen = set()
    for i in range(relocs):
        at, kind, value = struct.unpack_from('<IIQ', data, 128 + count * 24 + i * 16)
        if (kind != 1 or at & 7 or at in seen or not inside(at, 8, 2)
                or value > 0xffffffff or not inside(value, 1, 1)):
            raise ValueError('invalid module relocation')
        seen.add(at)
    return dict(package_bytes=size, image_bytes=image, entry_offset=entry,
                segments=count, relocations=relocs, required=required, entry_abi=1,
                native_stack_bytes=native_stack, user_stack_bytes=user_stack)
