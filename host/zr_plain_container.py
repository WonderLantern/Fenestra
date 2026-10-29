"""Parse the observed ZR plaintext component container without authentication.

The container's CRC-16 fields are public integrity checks. This does not encode
the outer update, construct authentication fields, modify firmware or flash it.
"""
import binascii
import hashlib
import re
import struct

MAX_BYTES = 128 * 1024 * 1024


def valid_name(name, seen):
    if (not re.fullmatch(rb"[A-Za-z0-9_.-]{1,16}", name) or name in (b".", b"..") or
            name.lower() in seen):
        raise ValueError("unsafe or duplicate component name")
    seen.add(name.lower())


def inspect(package):
    return _inspect(package, require_crcs=True)


def _inspect(package, *, require_crcs):
    """Always validate geometry/padding; only the explicit repair path skips CRC rejection."""
    if not 80 <= len(package) <= MAX_BYTES or package[:32] != b" " * 32:
        raise ValueError("invalid bounded plaintext container prefix")
    count, header = struct.unpack_from(">II", package, 32)
    if not 1 <= count <= 8 or header != 48 + 32 * count or header > len(package) - 16:
        raise ValueError("invalid component directory geometry")
    if package[40:48] != bytes(8):
        raise ValueError("nonzero directory reserved fields")
    previous, seen, rows = header, set(), []
    for i in range(count):
        label, offset, size, reserved_a, reserved_b = struct.unpack_from(">16s4I", package, 48 + 32 * i)
        name = label.rstrip(b"\0")
        valid_name(name, seen)
        if (reserved_a or reserved_b or offset != ((previous + 15) & ~15) or
                size < 2 or offset + size > len(package) - 16):
            raise ValueError("invalid ordered/aligned component bounds")
        if any(package[previous:offset]):
            raise ValueError("nonzero component alignment padding")
        contents = memoryview(package)[offset:offset + size]
        crc_ok = binascii.crc_hqx(contents, 0) == 0
        if require_crcs and not crc_ok:
            raise ValueError("component CRC-16 failed: " + name.decode("ascii"))
        rows.append({"name": name.decode("ascii"), "offset": offset, "bytes_with_crc": size,
                     "payload_bytes": size - 2, "payload_sha256": hashlib.sha256(contents[:-2]).hexdigest(),
                     "crc16_verified": crc_ok})
        previous = offset + size
    if ((previous + 15) & ~15) != len(package) - 16 or any(package[previous:len(package) - 16]):
        raise ValueError("invalid final alignment padding")
    whole_crc_ok = binascii.crc_hqx(memoryview(package)[:-14], 0) == 0
    if package[-14:] != bytes(14) or (require_crcs and not whole_crc_ok):
        raise ValueError("whole-container CRC-16 or trailer failed")
    return {"bytes": len(package), "sha256": hashlib.sha256(package).hexdigest(),
            "header_bytes": header, "component_count": count, "components": rows,
            "whole_container_crc16_verified": whole_crc_ok, "outer_encoding_verified": False,
            "authentication_verified": False, "flashable": False}


def repair_crcs(package):
    """Recompute checksum fields after validating every directory entry and padding.

    Intended for explicitly edited plaintext. This cannot recover damaged payloads
    or establish their provenance. Only the component/body CRC bytes may change.
    The caller must still enforce its target/component identity requirements.
    """
    before = _inspect(package, require_crcs=False)
    repaired = bytearray(package)
    changes = []

    def replace_crc(name, start, end):
        value = binascii.crc_hqx(memoryview(repaired)[start:end], 0).to_bytes(2, "big")
        old = bytes(repaired[end:end + 2])
        if old != value:
            changes.append({"name": name, "offset": end, "before": old.hex(), "after": value.hex()})
            repaired[end:end + 2] = value

    for row in before["components"]:
        replace_crc(row["name"], row["offset"], row["offset"] + row["payload_bytes"])
    replace_crc("whole-container", 0, len(repaired) - 16)
    result = bytes(repaired)
    after = inspect(result)
    if [r["payload_sha256"] for r in before["components"]] != [r["payload_sha256"] for r in after["components"]]:
        raise ValueError("CRC repair changed a component payload")
    return result, {"input_sha256": before["sha256"], "output_sha256": after["sha256"],
                    "changed_fields": changes, "component_payloads_preserved": True,
                    "directory_and_padding_validated": True}


def serialize(components):
    """Normal plaintext-directory/CRC serialization; no outer update output.

    Each entry is (ASCII filename, payload without its two-byte CRC).
    This generic helper assigns no build target and supplies no authentication.
    """
    if not 1 <= len(components) <= 8:
        raise ValueError("bounded nonempty component list required")
    header = 48 + 32 * len(components)
    out = bytearray(b" " * 32 + struct.pack(">II", len(components), header) + bytes(header - 40))
    seen = set()
    for at, (name, payload) in enumerate(components):
        label = name.encode("ascii")
        valid_name(label, seen)
        if not isinstance(payload, (bytes, bytearray, memoryview)):
            raise ValueError("component payload must be bytes")
        size = (payload.nbytes if isinstance(payload, memoryview) else len(payload)) + 2
        offset = (len(out) + 15) & ~15
        if offset + size + 31 > MAX_BYTES:
            raise ValueError("plaintext container exceeds bounded capacity")
        payload = bytes(payload)
        out += bytes(offset - len(out))
        out += payload + binascii.crc_hqx(payload, 0).to_bytes(2, "big")
        struct.pack_into(">16s4I", out, 48 + at * 32, label, offset, size, 0, 0)
    out += bytes((-len(out)) % 16)
    out += binascii.crc_hqx(out, 0).to_bytes(2, "big") + bytes(14)
    return bytes(out)
