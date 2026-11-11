"""Public module requirements and linked-core compatibility profiles.

These are declared interface/ABI checks, not a signature, sandbox, or a claim
that a native device is idle or physically qualified.
"""
import hashlib
import struct

SCHEMA = 'fenestra.module-environment/v1'
FOOTER = 24
RECORD = 80
LIMIT = 256
KINDS = {'function': 1, 'table': 2, 'hook': 3, 'device': 4}
ABIS = {'u64x8': 1, 'bytes': 2, 'events': 3, 'device': 4, 'aapcs64': 5}
REASONS = {
    0: 'compatible', 1: 'invalid_package', 2: 'unsupported_format', 3: 'core_abi',
    4: 'isa', 5: 'target', 6: 'checksum', 7: 'capabilities', 8: 'entry_abi',
    9: 'invalid_requirements', 10: 'environment_unavailable', 11: 'service_abi',
    12: 'interface_missing', 13: 'interface_kind', 14: 'interface_abi',
    15: 'interface_version', 16: 'interface_unavailable',
}


class CompatibilityError(ValueError):
    def __init__(self, reason, *, binding='', expected=0, actual=0):
        self.issue = dict(reason=REASONS[reason], code=reason, binding=binding, expected=expected, actual=actual)
        super().__init__(f"{self.issue['reason']}: {binding or 'module'} (expected {expected}, actual {actual})")


def requirement(value):
    if not isinstance(value, dict) or set(value) != {'name', 'kind', 'abi', 'version'}:
        raise ValueError('requirement must contain name, kind, abi and exact version')
    name = value['name']
    if (not isinstance(name, str) or not 0 < len(name) < 64 or
            any(ord(c) < 32 or ord(c) > 126 for c in name)):
        raise ValueError('interface name must be 1..63 printable ASCII bytes')
    for field, maximum in (('kind', 4), ('abi', 5), ('version', 0xffffffff)):
        if type(value[field]) is not int or not 1 <= value[field] <= maximum:
            raise ValueError('invalid interface ' + field)
    return dict(value)


def parse_requirement(text):
    try:
        name, kind, abi, version = text.rsplit(':', 3)
        return requirement(dict(name=name, kind=KINDS[kind], abi=ABIS[abi], version=int(version, 0)))
    except (ValueError, KeyError) as error:
        raise ValueError('expected NAME:function|table|hook|device:u64x8|bytes|events|device|aapcs64:VERSION') from error


def encode(requirements, service_abi=1):
    if type(service_abi) is not int or not 1 <= service_abi <= 0xffffffff:
        raise ValueError('invalid required service ABI')
    if not isinstance(requirements, (list, tuple)) or len(requirements) > LIMIT:
        raise ValueError('too many interface requirements')
    rows = [requirement(row) for row in requirements]
    if len({row['name'] for row in rows}) != len(rows):
        raise ValueError('duplicate interface requirement')
    return b''.join(struct.pack('<64s4I', row['name'].encode('ascii'), row['kind'], row['abi'], row['version'], 0)
                    for row in rows) + struct.pack('<8s4I', b'FXCMPT01', len(rows), RECORD, service_abi, 0)


def decode(data):
    if struct.unpack_from('<I', data, 8)[0] != 4:
        return len(data), 0, []
    if len(data) < 128 + FOOTER:
        raise CompatibilityError(9)
    magic, count, stride, service, reserved = struct.unpack_from('<8s4I', data, len(data) - FOOTER)
    if magic != b'FXCMPT01' or count > LIMIT or stride != RECORD or not service or reserved or count * stride > len(data) - 128 - FOOTER:
        raise CompatibilityError(9)
    start = len(data) - FOOTER - count * stride
    rows, names = [], set()
    for i in range(count):
        raw, kind, abi, version, reserved = struct.unpack_from('<64s4I', data, start + i * stride)
        at = raw.find(b'\0')
        if at <= 0 or any(raw[at:]) or reserved:
            raise CompatibilityError(9)
        try:
            row = requirement(dict(name=raw[:at].decode('ascii'), kind=kind, abi=abi, version=version))
        except (ValueError, UnicodeError) as error:
            raise CompatibilityError(9) from error
        if row['name'] in names:
            raise CompatibilityError(9)
        rows.append(row)
        names.add(row['name'])
    return start, service, rows


def validate_environment(value):
    if not isinstance(value, dict) or value.get('schema') != SCHEMA:
        raise ValueError('unsupported module compatibility profile')
    identity = value.get('module_identity')
    if not isinstance(identity, str) or len(identity) != 64:
        raise ValueError('invalid compatibility target identity')
    try:
        bytes.fromhex(identity)
    except ValueError as error:
        raise ValueError('invalid compatibility target identity') from error
    for field in ('core_abi', 'isa', 'service_abi', 'capabilities'):
        if type(value.get(field)) is not int or not 0 <= value[field] <= 0xffffffffffffffff:
            raise ValueError('invalid compatibility ' + field)
    formats = value.get('module_formats')
    if not isinstance(formats, list) or not formats or any(type(v) is not int or v not in (1, 2, 3, 4) for v in formats) or len(set(formats)) != len(formats):
        raise ValueError('invalid supported module formats')
    rows = value.get('interfaces')
    if not isinstance(rows, list):
        raise ValueError('interface table missing')
    names = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {'name', 'kind', 'abi', 'version', 'available', 'capabilities'}:
            raise ValueError('invalid interface profile row')
        requirement({key: row[key] for key in ('name', 'kind', 'abi', 'version')})
        if row['name'] in names or type(row['available']) is not bool or type(row['capabilities']) is not int or not 0 <= row['capabilities'] <= 0xffffffffffffffff:
            raise ValueError('invalid/duplicate interface profile row')
        names.add(row['name'])
    return value


def check_environment(data, environment, *, service=0, requirements=()):
    e = validate_environment(environment)
    version, _, core, isa = struct.unpack_from('<4I', data, 8)
    if version not in e['module_formats']:
        raise CompatibilityError(2, expected=max(e['module_formats']), actual=version)
    if core != e['core_abi']:
        raise CompatibilityError(3, expected=core, actual=e['core_abi'])
    if isa != e['isa']:
        raise CompatibilityError(4, expected=isa, actual=e['isa'])
    if data[24:56] != bytes.fromhex(e['module_identity']):
        raise CompatibilityError(5)
    grants = struct.unpack_from('<Q', data, 108)[0]
    if grants & ~e['capabilities']:
        raise CompatibilityError(7, expected=grants, actual=e['capabilities'])
    if not service:
        if e['service_abi']!=1:raise CompatibilityError(11,expected=1,actual=e['service_abi'])
        return
    if service != e['service_abi']:
        raise CompatibilityError(11, expected=service, actual=e['service_abi'])
    available = {row['name']: row for row in e['interfaces']}
    for requested in requirements:
        name = requested['name']
        if name not in available:
            raise CompatibilityError(12, binding=name, expected=requested['version'])
        found = available[name]
        for field, reason in (('kind', 13), ('abi', 14), ('version', 15)):
            if requested[field] != found[field]:
                raise CompatibilityError(reason, binding=name, expected=requested[field], actual=found[field])
        needed = found['capabilities'] | 0x200000
        if needed & ~grants:
            raise CompatibilityError(7, binding=name, expected=needed, actual=grants)
        if not found['available']:
            raise CompatibilityError(16, binding=name, expected=1)


def export_core(data, symbols, base, target):
    """Read the compiled registry with offsets exported by its own C compiler."""
    def read(address, size):
        at = address - base
        if at < 0 or size < 0 or at + size > len(data):
            raise ValueError('compatibility export leaves owned core image')
        return data[at:at + size]
    def number(address, width=4):
        return int.from_bytes(read(address, width), 'little')
    def string(address):
        raw = read(address, min(64, base + len(data) - address))
        end = raw.find(b'\0')
        if end <= 0:
            raise ValueError('invalid linked binding name')
        return raw[:end].decode('ascii')
    meta = struct.unpack('<23Q', read(symbols['fx_module_compatibility_metadata'], 23 * 8))
    magic, version, platform, count_at, identity_at, bindings_at, stride, name_at, kind_at, abi_at, version_at, flags_at, caps_at, address_at, read_at, call_at, resource_at, frame_at, core, isa, service, caps, formats = meta
    if (magic, version) != (0x4658434d50543031, 1) or not 0 < stride <= 256:
        raise ValueError('invalid linked compatibility metadata')
    count = number(platform + count_at)
    if not 0 <= count <= 65536:
        raise ValueError('invalid linked binding count')
    bindings = number(platform + bindings_at, 8)
    interfaces = []
    for i in range(count):
        at = bindings + i * stride
        name = string(number(at + name_at, 8))
        kind, abi, flags = number(at + kind_at), number(at + abi_at), number(at + flags_at)
        available = bool(number(at + address_at, 8))
        if kind == 1:
            available &= bool(number(platform + (frame_at if abi == 5 else call_at), 8))
        elif kind in (3, 4):
            available &= bool(number(platform + resource_at, 8))
        elif kind == 2 and flags & 1:
            available &= bool(number(platform + read_at, 8))
        interfaces.append(dict(name=name, kind=kind, abi=abi, version=number(at + version_at), available=available,
                               capabilities=number(at + caps_at) | 0x200000))
    return validate_environment(dict(schema=SCHEMA, target=target, module_identity=read(platform + identity_at, 32).hex(),
        core_abi=core, isa=isa, service_abi=service, capabilities=caps, module_formats=[v for v in range(1, 5) if formats & (1 << v)],
        interfaces=interfaces, core_sha256=hashlib.sha256(data).hexdigest(), physical_validation=False))
