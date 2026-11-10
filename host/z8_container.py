"""Pinned Z8 C3.11 input validation and optional container decoding.

The optional encrypted-package reader is an explicitly selected external local
project. Its source is neither copied nor bundled here. Package identity is
checked before loading it, its source digest is pinned, and its returned MAIN
is checked again independently. Standalone MAIN inputs need only stdlib.
"""
import binascii
import hashlib
import types
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET_PATH = ROOT / 'targets' / 'nikon-z8-c311.json'
TARGET = json.loads(TARGET_PATH.read_text(encoding='utf-8'))
IDENTITIES = TARGET['identities']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verify_identity(data, identity, label):
    if len(data) != identity['bytes'] or sha(data) != identity['sha256']:
        raise ValueError(label + ' length/SHA-256 differs from pinned original')


def identify(data):
    """Exact whole-input identity; filenames and header resemblance are unused."""
    candidates = [kind for kind, spec in IDENTITIES.items() if len(data) == spec['bytes']]
    if len(candidates) != 1:
        raise ValueError('unsupported Z8 input length')
    kind = candidates[0]
    verify_identity(data, IDENTITIES[kind], kind)
    if kind == 'component' and binascii.crc_hqx(data, 0):
        raise ValueError('MAIN component CRC16 differs')
    return kind


def read_input(path):
    path = Path(path)
    # Bound even a file that changes while being read. Never open output here.
    maximum = max(spec['bytes'] for spec in IDENTITIES.values())
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    return data, identify(data)


def verify_application(data):
    verify_identity(data, IDENTITIES['application'], 'application')
    return data


def _load_external_reader(project):
    if project is None:
        raise ValueError('encrypted package extraction requires explicit --external-project; or provide the original MAIN component/application')
    project = Path(project).resolve(strict=True)
    codec = TARGET['external_reader']

    def checked_input(spec):
        path = (project / spec['relative_path']).resolve(strict=True)
        if not path.is_relative_to(project):
            raise ValueError('external codec inputs must stay within the selected project')
        data = path.read_bytes()
        if sha(data) != spec['sha256']:
            raise ValueError('external codec SHA-256 differs: ' + spec['relative_path'])
        return path, data

    source, source_bytes = checked_input(codec)
    checked_input(codec['resource'])
    # Execute only the bytes just checked, without creating external __pycache__.
    module = types.ModuleType('_fenestra_external_z8_reader')
    module.__file__ = str(source)
    exec(compile(source_bytes, str(source), 'exec'), module.__dict__)
    return module.Package, dict(source=str(source), source_sha256=codec['sha256'],
                               resource_sha256=codec['resource']['sha256'],
                               license=TARGET['external_reader']['license'],
                               invocation='Package(raw); read eg/modules only')


def _external_application(raw, project):
    reader, provenance = _load_external_reader(project)
    decoded = reader(raw)
    rows = decoded.modules
    if rows != TARGET['modules']:
        raise ValueError('external reader returned a different component directory')
    component = bytes(decoded.eg)
    verify_identity(component, IDENTITIES['component'], 'extracted MAIN component')
    if binascii.crc_hqx(component, 0):
        raise ValueError('extracted MAIN CRC16 differs')
    return verify_application(component[:-2]), rows, provenance


def application_from_bytes(data, *, external_project=None):
    kind = identify(data)
    rows, provenance = None, None
    if kind == 'package':
        application, rows, provenance = _external_application(data, external_project)
    elif kind == 'component':
        application = verify_application(data[:-2])
    else:
        application = verify_application(data)
    receipt = dict(target=TARGET['target'], model=TARGET['model'], version=TARGET['version'],
                   runtime_ready=False, flashable_package_produced=False,
                   input_kind=kind, input_bytes=len(data), input_sha256=sha(data),
                   module_name=TARGET['module_name'], application_bytes=len(application),
                   application_sha256=sha(application), embedded_main_verified=True)
    if rows is not None:
        receipt['components'] = rows
        receipt['external_reader'] = provenance
    return application, receipt
