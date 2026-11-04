"""Extract hash-pinned Zf C3.01 native-menu preimages into build/."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path

from zf_c301 import BASE, EG_SIZE, EG_SHA256, EG_PAYLOAD_SHA256
from zf_native_menu import MENU_HOOKS

ROOT = Path(__file__).resolve().parents[1]


def prepare(application, output):
    path, out = Path(application), Path(output).resolve()
    build = (ROOT / 'build').resolve()
    if not out.is_relative_to(build) or out == build or out.exists():
        raise ValueError('choose a new directory below build/')
    data = path.read_bytes()
    if len(data) == EG_SIZE:
        if hashlib.sha256(data).hexdigest() != EG_SHA256 or binascii.crc_hqx(data, 0):
            raise ValueError('original Zf module identity or CRC differs')
        payload = data[:-2]
    elif len(data) == EG_SIZE - 2 and hashlib.sha256(data).hexdigest() == EG_PAYLOAD_SHA256:
        payload = data
    else:
        raise ValueError('original Zf application identity differs')
    out.mkdir(parents=True)
    rows = {}
    for name, spec in MENU_HOOKS.items():
        at = spec['address'] - BASE
        raw = payload[at:at + spec['bytes']]
        digest = hashlib.sha256(raw).hexdigest()
        if digest != spec['sha256']:
            raise ValueError('menu entry window differs: ' + name)
        symbol = 'fx_zf_menu_' + name + '_original'
        with (out / (symbol + '.inc')).open('x', encoding='ascii', newline='\n') as stream:
            stream.write('.byte ' + ','.join(str(byte) for byte in raw) + '\n')
        if name in ('text', 'navigate'):
            with (out / ('fx_zf_menu_' + name + '_prefix.inc')).open('x', encoding='ascii', newline='\n') as stream:
                stream.write('.byte ' + ','.join(str(byte) for byte in raw[:16]) + '\n')
        rows[name] = dict(symbol=symbol, address=hex(spec['address']), bytes=len(raw), sha256=digest,
                          complete_function_verified=False, runtime_hook_verified=False)
    result = dict(schema='fenestra.zf-c301-menu-preimages/v1', target='zf-c301',
                  application_payload_sha256=hashlib.sha256(payload).hexdigest(), sites=rows,
                  runtime_qualified=False, firmware_output_generated=False,
                  installation_hook_generated=False)
    (out / 'menu-preimages.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.application, args.output_dir), indent=2))


if __name__ == '__main__':
    main()
