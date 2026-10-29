"""Decode copied FXI1 endpoint metadata; imports never access a camera.

Build identity is the source/build-configuration digest, not a measured hash of
installed Flash. Application and module-target identities are separate fields.
"""
import struct

IDENTITY_OPERATION=9


def parse_identity(raw):
    if len(raw)!=256 or raw[:4]!=b'FXI1':raise ValueError('endpoint identity length/magic')
    version,size,protocol,service,platform,kind,caps,firmware,module,flags=struct.unpack_from('<10I',raw,4)
    if (version,size,protocol,kind)!=(1,256,2,1) or not service or not platform or not module or not firmware:
        raise ValueError('endpoint identity schema/ABI')
    if not caps&(1<<(IDENTITY_OPERATION-1)) or any(raw[44:64]) or any(raw[240:256]):
        raise ValueError('endpoint identity capabilities/reserved')
    def text(offset,size):
        field=raw[offset:offset+size];end=field.find(b'\0')
        if end<=0 or any(field[end:]):raise ValueError('endpoint identity text/padding')
        return field[:end].decode('utf-8',errors='strict')
    hashes=[raw[start:start+32] for start in (64,96,128)]
    if any(not any(h) for h in hashes):raise ValueError('endpoint identity digest unavailable')
    return dict(schema=version,protocol=protocol,service_abi=service,platform_abi=platform,
        module_abi=module,capabilities=caps,firmware_code=firmware,flags=flags,
        application_sha256=hashes[0].hex(),module_target_sha256=hashes[1].hex(),
        build_identity=hashes[2].hex(),build_identity_kind='source-and-build-configuration/v1',
        model=text(160,48),firmware=text(208,32))
