"""Prepare only the original Zf SYSCTL entry window for an experimental trampoline."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path

from zf_c301 import BASE, EG_SIZE, EG_SHA256, EG_PAYLOAD_SHA256, SYSCTL_DISPATCH

ROOT = Path(__file__).resolve().parents[1]
PREFIX_SHA256 = '90f0a19094d99d64fd16a94f4a89813b33637eea1a3d08c22a65f1f7368de5ec'
SPAN_SHA256 = 'a73f06ab779cb5bf8138c5d61408d0b4173313ae976c92c52a6752ad68c5d17f'


def prepare(application, output):
    path, out = Path(application), Path(output).resolve()
    build = (ROOT / 'build').resolve()
    if not out.is_relative_to(build) or out == build or out.exists():
        raise ValueError('choose a new directory below build/')
    if path.stat().st_size not in (EG_SIZE, EG_SIZE - 2):
        raise ValueError('expected the pinned original Zf application')
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) == EG_SIZE:
        if digest != EG_SHA256 or binascii.crc_hqx(data, 0):
            raise ValueError('original Zf module identity or CRC differs')
        data = data[:-2]
    elif len(data) != EG_SIZE - 2 or digest != EG_PAYLOAD_SHA256:
        raise ValueError('original Zf application identity differs')
    offset = SYSCTL_DISPATCH - BASE
    if offset < 0 or offset + 268 > len(data):
        raise ValueError('original SYSCTL candidate outside application')
    prefix, span = data[offset:offset + 16], data[offset:offset + 268]
    if hashlib.sha256(prefix).hexdigest() != PREFIX_SHA256 or hashlib.sha256(span).hexdigest() != SPAN_SHA256:
        raise ValueError('original SYSCTL instruction window differs')
    out.mkdir(parents=True, exist_ok=False)
    with (out / 'fx_zf_system_original.inc').open('x', encoding='ascii', newline='\n') as f:
        f.write('.byte ' + ','.join(str(b) for b in prefix) + '\n')
    return dict(target='zf-c301', application_payload_sha256=EG_PAYLOAD_SHA256,
                original_entry=hex(SYSCTL_DISPATCH), continuation=hex(SYSCTL_DISPATCH + 16),
                prefix_sha256=PREFIX_SHA256, span_sha256=SPAN_SHA256,
                output=str(out / 'fx_zf_system_original.inc'), runtime_qualified=False,
                installation_hook_generated=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.application, args.output_dir), indent=2))


if __name__ == '__main__':
    main()
