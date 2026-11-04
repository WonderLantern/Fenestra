"""Extract three pinned Zf C3.01 PTP preimages into a private, new directory."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = 0x40160000
FULL_SIZE = 79144343
FULL_SHA256 = '0b53631396314e17e5b5a46a6758a35ae521c1e8bddc2fb59b61bcf72e4889f8'
PAYLOAD_SHA256 = '6891edd22d9a40042e4718480d29082682bbd12aea172c51f4f164b97f74fe08'
SITES = {
    'fx_zf_ptp_original_lookup': (0x407f5000, 'af8f6f1325c67bfa89af71adfb03816b012ce2d2932587e501df7e6351f69e40'),
    'fx_zf_ptp_original_session': (0x407f39b8, '632d329fefdbee16dd116345391f8a602a5e58bab7b224ed2c6992f84140d2f6'),
    'fx_zf_ptp_original_operations': (0x40361d08, 'de1533bb4fe111cba176aba3e901507841132aefd0bb24e99ca4d220a9c53152'),
}


def prepare(application, output, allow_build=False):
    application = Path(application)
    if application.stat().st_size not in (FULL_SIZE, FULL_SIZE - 2):
        raise ValueError('expected complete Zf C3.01 application')
    image = application.read_bytes()
    identity = hashlib.sha256(image).hexdigest()
    if len(image) == FULL_SIZE:
        if identity != FULL_SHA256 or binascii.crc_hqx(image, 0):
            raise ValueError('complete original Zf module identity differs')
    elif identity != PAYLOAD_SHA256:
        raise ValueError('original Zf application identity differs')
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError('choose a new private directory outside the source tree')
    output = output.resolve()
    allowed_build = output.is_relative_to(ROOT / 'build')
    if (output.is_relative_to(ROOT) and not (allow_build and allowed_build)) or not output.parent.is_dir():
        raise ValueError('choose a new private directory outside the source tree')
    windows = {}
    for name, (address, expected) in SITES.items():
        original = image[address-BASE:address-BASE+16]
        if len(original) != 16 or hashlib.sha256(original).hexdigest() != expected:
            raise ValueError('original PTP entry window differs: '+name)
        windows[name] = original
    output.mkdir()
    for name, original in windows.items():
        with (output/(name+'.inc')).open('x', encoding='ascii', newline='\n') as f:
            f.write('.byte '+','.join(map(str, original))+'\n')
    return dict(application_sha256=identity, sites={name: dict(address=hex(SITES[name][0]), bytes=16,
                 sha256=SITES[name][1]) for name in windows},
                 runtime_qualified=False, firmware_output_generated=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args=parser.parse_args()
    print(json.dumps(prepare(args.application,args.output_dir),indent=2))


if __name__=='__main__':
    main()
