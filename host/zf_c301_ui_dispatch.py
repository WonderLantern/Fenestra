"""Prepare the bounded, static-only Zf UI wait-loop candidate windows."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path

from zf_c301 import BASE, EG_SIZE, EG_SHA256, EG_PAYLOAD_SHA256

ROOT = Path(__file__).resolve().parents[1]
# Bounded synchronous callees, not proven UI task loops. The legacy
# wait_wrapper/set_wrapper fields denote BL instruction sites, not entries.
UI_CANDIDATES = (
    dict(name='candidate_a', start=0x403e5714, end=0x403e59c4,
         wait_wrapper=0x403e5910, set_wrapper=0x403e5920,
         mask=0x80, timeout=10000, sha256='7886986f6d3e299063f8f6080931ec8801ed51ec6fe0c20ed81c87799d979129'),
    dict(name='candidate_b', start=0x403e6310, end=0x403e6474,
         wait_wrapper=0x403e63ac, set_wrapper=0x403e63bc,
         mask=0x80, timeout=10000, sha256='4c769301bf71c9f86f8a2796cdf5ee799b2baffaa70584f8baf4ff95e86a97ba'),
)


def _payload(application):
    path = Path(application)
    if path.stat().st_size not in (EG_SIZE, EG_SIZE - 2):
        raise ValueError('expected the pinned original Zf application')
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) == EG_SIZE:
        if digest != EG_SHA256 or binascii.crc_hqx(data, 0):
            raise ValueError('original Zf module identity or CRC differs')
        return data[:-2]
    if digest != EG_PAYLOAD_SHA256:
        raise ValueError('original Zf application identity differs')
    return data


def _write_include(path, data):
    with path.open('x', encoding='ascii', newline='\n') as stream:
        for offset in range(0, len(data), 16):
            stream.write('    ' + ','.join('0x%02x' % value for value in data[offset:offset + 16]) + ',\n')


def prepare(application, output):
    out = Path(output).resolve()
    build = (ROOT / 'build').resolve()
    if not out.is_relative_to(build) or out == build or out.exists():
        raise ValueError('choose a new directory below build/')
    data = _payload(application)
    rows = []
    for candidate in UI_CANDIDATES:
        start = candidate['start'] - BASE
        end = candidate['end'] - BASE
        if start < 0 or end <= start or end > len(data):
            raise ValueError('Zf UI candidate outside application')
        window = data[start:end]
        digest = hashlib.sha256(window).hexdigest()
        if digest != candidate['sha256']:
            raise ValueError('Zf UI candidate window differs')
        rows.append(dict(candidate, bytes=len(window), complete_function_fde=False,
                         ui_owner_verified=False))
    out.mkdir(parents=True, exist_ok=False)
    for row in rows:
        _write_include(out / ('fx_zf_ui_%s.inc' % row['name']),
                       data[row['start'] - BASE:row['end'] - BASE])
    result = dict(schema='fenestra.zf-c301-ui-dispatch-candidates/v1', target='zf-c301',
                  application_payload_sha256=EG_PAYLOAD_SHA256, candidates=rows,
                  runtime_qualified=False, installation_hook_generated=False,
                  status='STATIC_CANDIDATES_ONLY',
                  limits=['function boundaries and ABI are not proven',
                          'event/task identity and UI ownership are not proven',
                          'no hook or runtime dispatch path is generated'])
    (out / 'ui-dispatch-preimages.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.application, args.output_dir), indent=2))


if __name__ == '__main__':
    main()
