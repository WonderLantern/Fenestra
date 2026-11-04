"""Local-only Zf C3.01 package reconstruction.

The source contains only public layout rules. K8, the body XOR stream and
optional authentication material are supplied from local paths at runtime.
"""
import binascii
import hashlib
import json
from pathlib import Path

import zf_c301


VERSION = "3.01"
PACKAGE_OFFSET = zf_c301.BODY_OFFSET
HEADER_DIGEST_RANGE = slice(0x240, 0x254)
HEADER_PRESERVED_RANGE = slice(0x254, PACKAGE_OFFSET)
AUTH_STATUS = "PACKAGED_INTERNAL_INTEGRITY_ONLY"


def _read(path, *, size=None, label="material"):
    path = Path(path)
    if not path.is_file():
        raise ValueError(f"{label} is not a regular file")
    data = path.read_bytes()
    if size is not None and len(data) != size:
        raise ValueError(f"{label} must be exactly {size} bytes")
    return data


def _binding(path, data, *, sensitive=False):
    result = {"bytes": len(data), "present": True, "sensitive": sensitive}
    if sensitive:
        result["local_only"] = True
    else:
        result["sha256"] = hashlib.sha256(data).hexdigest()
        result["name"] = Path(path).name
    return result


def _xor(data, stream):
    if len(data) != len(stream):
        raise ValueError("Zf body and XOR stream lengths differ")
    return bytes(a ^ b for a, b in zip(data, stream))


def _module_rows(body):
    rows = zf_c301.inspect_body(body)
    return {row["name"]: row for row in rows}, rows


def _replace_eg(body, candidate):
    rows, ordered = _module_rows(body)
    row = rows.get(zf_c301.EG_NAME)
    if row is None:
        raise ValueError("Zf EG component is missing")
    if len(candidate) == zf_c301.EG_SIZE:
        payload = candidate[:-2]
    elif len(candidate) == zf_c301.EG_SIZE - 2:
        payload = candidate
    else:
        raise ValueError("candidate EG must include its 2-byte CRC or omit it")
    if len(payload) != row["plaintext_payload_bytes"]:
        raise ValueError("candidate EG payload length differs")
    module = payload + binascii.crc_hqx(payload, 0).to_bytes(2, "big")
    result = bytearray(body)
    start = row["plaintext_body_offset"]
    result[start:start + len(module)] = module
    # Zf stores a body CRC followed by fourteen zero bytes.
    result[-16:-14] = binascii.crc_hqx(result[:-16], 0).to_bytes(2, "big")
    result[-14:] = bytes(14)
    rebuilt = bytes(result)
    _module_rows(rebuilt)
    return rebuilt, row, ordered


def _verify_version(version):
    if str(version) != VERSION:
        raise ValueError(f"Zf package version must remain C {VERSION}")


def _write_new(path, data):
    path = Path(path)
    if path.exists():
        raise ValueError(f"refusing to overwrite existing output: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def repack(package, body, eg, k8, stream, output, *, version=VERSION):
    """Rebuild an equal-sized Zf package using caller-provided local material."""
    _verify_version(version)
    raw = _read(package, size=zf_c301.PACKAGE_SIZE, label="original package")
    if hashlib.sha256(raw).hexdigest() != zf_c301.PACKAGE_SHA256:
        raise ValueError("original package SHA-256 differs")
    original_body = _read(body, size=zf_c301.BODY_SIZE, label="original body")
    if hashlib.sha256(original_body).hexdigest() != zf_c301.BODY_SHA256:
        raise ValueError("original body SHA-256 differs")
    k8_bytes = _read(k8, size=8, label="Zf K8")
    xor_stream = _read(stream, size=zf_c301.BODY_SIZE, label="Zf XOR stream")
    if hashlib.sha1(original_body + k8_bytes).digest() != raw[HEADER_DIGEST_RANGE]:
        raise ValueError("K8 does not reproduce the original package digest")
    if _xor(raw[PACKAGE_OFFSET:], xor_stream) != original_body:
        raise ValueError("XOR stream does not decode the original package body")
    zf_c301.inspect_body(original_body)
    candidate_eg = _read(eg, label="candidate EG")
    changed_body, eg_row, _ = _replace_eg(original_body, candidate_eg)
    digest = hashlib.sha1(changed_body + k8_bytes).digest()
    candidate = raw[:HEADER_DIGEST_RANGE.start] + digest + raw[HEADER_DIGEST_RANGE.stop:PACKAGE_OFFSET]
    candidate += _xor(changed_body, xor_stream)
    if len(candidate) != len(raw):
        raise ValueError("reconstructed package length changed")
    if _xor(candidate[PACKAGE_OFFSET:], xor_stream) != changed_body:
        raise ValueError("reconstructed package does not round-trip")
    if hashlib.sha1(changed_body + k8_bytes).digest() != candidate[HEADER_DIGEST_RANGE]:
        raise ValueError("reconstructed package digest mismatch")
    if hashlib.sha256(candidate).hexdigest() == hashlib.sha256(raw).hexdigest():
        raise ValueError("candidate package is byte-identical to original")

    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    package_path = output / "Z_f_0301.fenestra.candidate.bin"
    body_path = output / "candidate.body.bin"
    eg_path = output / "candidate.eg.bin"
    _write_new(package_path, candidate)
    _write_new(body_path, changed_body)
    _write_new(eg_path, changed_body[eg_row["plaintext_body_offset"]:
                                    eg_row["plaintext_body_offset"] + eg_row["plaintext_bytes_with_crc"]])

    receipt = {
        "schema": "fenestra.zf-c301-repack/v1",
        "target": "zf-c301",
        "model": "Nikon Zf",
        "version": f"C {VERSION}",
        "status": AUTH_STATUS,
        "flashable": False,
        "inputs": {
            "package": _binding(package, raw),
            "body": _binding(body, original_body),
            "candidate_eg": _binding(eg, candidate_eg),
            "k8": _binding(k8, k8_bytes, sensitive=True),
            "xor_stream": _binding(stream, xor_stream, sensitive=True),
            "authentication_material": {"present": False},
        },
        "outputs": {
            "package": _binding(package_path, candidate),
            "body": _binding(body_path, changed_body),
            "eg": _binding(eg_path, bytes(eg_path.read_bytes())),
        },
        "checks": {
            "original_package_sha256": True,
            "original_body_sha256": True,
            "original_k8_digest": True,
            "original_xor_roundtrip": True,
            "module_crc16_xmodem": True,
            "body_crc16_xmodem": True,
            "candidate_k8_digest": True,
            "equal_size": True,
            "outer_authentication_applied": False,
        },
        "module": {"name": zf_c301.EG_NAME, "package_offset": PACKAGE_OFFSET + eg_row["plaintext_body_offset"],
                   "bytes_with_crc": eg_row["plaintext_bytes_with_crc"]},
        "notes": ["K8 and XOR stream are external local inputs and are not embedded in source or documentation.",
                  "The 20-byte K8-derived package digest was recomputed; vendor outer authentication/signature was not applied.",
                  "The package header's version-related bytes are preserved from the pinned C3.01 original; no independent field parser is available."],
    }
    receipt_path = output / "repack.receipt.json"
    _write_new(receipt_path, (json.dumps(receipt, indent=2) + "\n").encode("utf-8"))
    return receipt
