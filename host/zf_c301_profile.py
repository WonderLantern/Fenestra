"""Load the hardware-tested Zf C3.01 platform profile."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE_PATH = ROOT / 'targets/nikon-zf-c301.json'


def load_profile(path=PROFILE_PATH):
    profile = json.loads(Path(path).read_text(encoding='utf-8'))
    if profile.get('schema') != 'fenestra.platform-profile/v1':
        raise ValueError('unexpected Zf profile schema')
    if (profile.get('model'), profile.get('firmware'), profile.get('support_state')) != (
            'Nikon Zf', 'C 3.01', 'link81-hardware-tested'):
        raise ValueError('profile is not the pinned Zf C3.01 target')
    module = profile.get('original_eg_module', {})
    if module.get('payload_sha256') != '6891edd22d9a40042e4718480d29082682bbd12aea172c51f4f164b97f74fe08':
        raise ValueError('Zf module identity differs')
    bindings = profile.get('core_bindings', {}).get('rtos_functions', {})
    if (bindings.get('memory_allocate'), bindings.get('memory_release'),
            bindings.get('same_address_from_z9'), bindings.get('zf_specific_rebindings')) != (
                '0x40d31898', '0x40d318d8', 58, 2):
        raise ValueError('Zf RTOS binding correction differs')
    return profile


def verify_application(path, profile=None):
    profile = load_profile() if profile is None else profile
    data = Path(path).read_bytes()
    module = profile['original_eg_module']
    if len(data) == module['bytes_with_crc']:
        if hashlib.sha256(data).hexdigest() != module['sha256_with_crc']:
            raise ValueError('Zf EG with CRC differs')
        payload = data[:-2]
    elif len(data) == module['payload_bytes']:
        payload = data
    else:
        raise ValueError('Zf EG length differs')
    if hashlib.sha256(payload).hexdigest() != module['payload_sha256']:
        raise ValueError('Zf EG payload differs')
    return dict(profile='zf-c301', bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                payload_sha256=hashlib.sha256(payload).hexdigest(), runtime_qualified=False,
                firmware_output_generated=False)
