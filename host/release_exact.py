"""Reproduce a qualified release identity, then require exact application bytes.

Current source provenance stays in the ordinary build receipt. Runtime identity
normalization is a separate step, never a substitute for a full-image hash.
"""
import hashlib
import json
from pathlib import Path
from cipher_patch import read_bounded, sha

ROOT = Path(__file__).resolve().parents[1]

def reference(target):
    return json.loads((ROOT/'releases/r3.json').read_text(encoding='utf-8'))[target]

def normalize(target, candidate_file):
    candidate_file = Path(candidate_file)
    proof = json.loads((candidate_file.parent/'verification.json').read_text(encoding='utf-8'))
    data = bytearray(read_bounded(candidate_file))
    if proof.get('passed') is not True or proof.get('candidate_sha256') != sha(data):
        raise ValueError('verified local candidate required for r3 identity normalization')
    ref = reference(target)
    identity = proof['debug_identity']
    old, fixed = bytes.fromhex(identity['identity']), bytes.fromhex(ref['runtime_identity'])
    sites = [int(a, 16)-0x40160000 for a in identity['linked_copies']]
    if not sites or len(sites) != len(set(sites)):
        raise ValueError('runtime identity sites missing/duplicated')
    for at in sites:
        if not 0 <= at <= len(data)-32 or data[at:at+32] != old:
            raise ValueError('runtime identity preimage differs')
        data[at:at+32] = fixed
    if sha(data) != ref['application_sha256']:
        raise ValueError('compiled application is not byte-identical to r3 after identity normalization; no release patch produced')
    return bytes(data), dict(reference=ref['reference'], application_sha256=ref['application_sha256'],
                            current_source_identity=old.hex(), released_runtime_identity=fixed.hex(),
                            identity_sites=len(sites), exact_application_verified=True)

def envelope_r3(material, key_path):
    """Reproduce one existing envelope with user-provided RSA public material.

    This is deliberately NOT an API for encrypting arbitrary new secrets with
    fixed PKCS#1 padding. Exact output is pinned to the existing r3 envelope.
    """
    from cryptography.hazmat.primitives.serialization import load_pem_public_key, load_pem_private_key
    from cryptography.hazmat.primitives.asymmetric.rsa import RSAPublicKey, RSAPrivateKey
    if key_path is None:
        raise ValueError('ZR r3 reproduction requires --envelope-key (user RSA public or private PEM)')
    pem = read_bounded(key_path, 16384)
    try:
        key = load_pem_public_key(pem)
    except ValueError:
        key = load_pem_private_key(pem, password=None)
        if not isinstance(key, RSAPrivateKey):
            raise ValueError('RSA key required')
        key = key.public_key()
    if not isinstance(key, RSAPublicKey) or key.key_size != 2048 or len(material) != 48:
        raise ValueError('RSA-2048 public material and 48-byte body key required')
    numbers, ref = key.public_numbers(), reference('zr-c110')
    if sha(numbers.n.to_bytes(256, 'big')) != ref['public_modulus_sha256']:
        raise ValueError('RSA public identity differs from reference release')
    padding = bytes.fromhex(ref['envelope_padding'])
    if len(padding) != 205 or not all(padding):
        raise ValueError('invalid reference padding')
    encoded = b'\0\2' + padding + b'\0' + material
    envelope = pow(int.from_bytes(encoded, 'big'), numbers.e, numbers.n).to_bytes(256, 'big')
    if sha(envelope) != ref['envelope_sha256']:
        raise ValueError('reference envelope mismatch')
    return envelope

def finalize(target, candidate, material, envelope_key=None):
    data = bytearray(candidate)
    if target == 'zr-c110':
        data[32:288] = envelope_r3(material, envelope_key)
    if sha(data) != reference(target)['package_sha256']:
        raise ValueError('final update differs from r3; no release patch produced')
    return bytes(data)
