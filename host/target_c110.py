"""Fixed C1.10 build identity. This is not camera update authentication.

Do not obtain the accepted fingerprint from mutable profile fields. All current
camera-specific entry points must reject a switched version or package identity.
The generic ELF/package parsing functions remain usable for offline tests.
"""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "C 1.10"
PACKAGE_BYTES = 117159984
PACKAGE_SHA256 = "43bf4f5f1278fa6b0a551ceb6fa4ee749dc74bbc8a5ef4fba19b30062184e546"


def require_profile(profile):
    if not isinstance(profile, dict):
        raise ValueError("ZR C1.10 profile must be an object")
    reference = profile.get("reference_update_package")
    if not isinstance(reference, dict):
        raise ValueError("ZR C1.10 package identity missing")
    if (profile.get("model") != "Nikon ZR" or
            reference.get("version") != VERSION or
            type(reference.get("bytes")) is not int or
            reference["bytes"] != PACKAGE_BYTES or
            reference.get("sha256") != PACKAGE_SHA256):
        raise ValueError("current target must match the independently pinned ZR C1.10 package")
    excluded = profile.get("disallowed_target_versions")
    if not isinstance(excluded, list) or "C 1.11" not in excluded:
        raise ValueError("current target must explicitly exclude C1.11")
    identity = profile.get("current_runtime_identity")
    if identity is not None and (not isinstance(identity, dict) or identity.get("version") != VERSION):
        raise ValueError("observed runtime version differs from the C1.10 build target")
    return profile


def load_profile(root=ROOT):
    return require_profile(json.loads((root / "targets/nikon-zr.json").read_text(encoding="utf-8")))


def require_module_target(package):
    # Only a target/header admission check. src/module.c validates the whole
    # package, segment/relocation ranges, body hash and external package pin.
    from module_header import entry_abi
    entry_abi(package)
    if package[24:56] != bytes.fromhex(PACKAGE_SHA256):
        raise ValueError("module header must target the independently pinned ZR C1.10 package")
