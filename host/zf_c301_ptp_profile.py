"""Read-only Zf C3.01 PTP entry profile; no transport or camera access."""
import hashlib
from pathlib import Path

from zf_c301_profile import load_profile
from zf_c301 import BASE

PTP_ENTRIES = {
    'lookup': (0x407F5000, 'af8f6f1325c67bfa89af71adfb03816b012ce2d2932587e501df7e6351f69e40'),
    'session': (0x407F39B8, '632d329fefdbee16dd116345391f8a602a5e58bab7b224ed2c6992f84140d2f6'),
    'operations': (0x40361D08, 'de1533bb4fe111cba176aba3e901507841132aefd0bb24e99ca4d220a9c53152'),
}


def inspect_application(path):
    profile = load_profile()
    data = Path(path).read_bytes()
    module = profile['original_eg_module']
    if len(data) == module['bytes_with_crc']:
        if hashlib.sha256(data).hexdigest() != module['sha256_with_crc']:
            raise ValueError('Zf EG with CRC differs')
        payload = data[:-2]
    elif len(data) == module['payload_bytes'] and hashlib.sha256(data).hexdigest() == module['payload_sha256']:
        payload = data
    else:
        raise ValueError('Zf EG identity differs')
    rows = []
    for name, (address, expected) in PTP_ENTRIES.items():
        raw = payload[address - BASE:address - BASE + 16]
        digest = hashlib.sha256(raw).hexdigest()
        if digest != expected:
            raise ValueError(f'Zf PTP entry window differs: {name}')
        rows.append(dict(name=name, address=hex(address), bytes=16, sha256=digest,
                         abi_qualified=False, runtime_qualified=False))
    return dict(schema='fenestra.zf-c301-ptp-profile/v1', target='zf-c301',
                application_payload_sha256=hashlib.sha256(payload).hexdigest(),
                entries=rows, experimental_default=False, real_usb_verified=False,
                firmware_output_generated=False,
                limitations=['entry windows do not prove complete function ABI',
                             'stock filter/session policy and owner lifecycle remain unqualified',
                             'no transport or camera operation'])
