"""Shared host identities and existing FXP2 transaction bounds; no device access."""
import re

PROFILES = {
    'zr-c110': (('ZR',), ('1.10', 'V1.10')),
    'z9-c532': (('Z 9', 'Z9'), ('5.32', 'V5.32')),
}


def check_profile(profile):
    if profile not in ('auto', *PROFILES):
        raise ValueError('PTP profile must be auto, zr-c110 or z9-c532')
    return profile


def identify(model, version, profile='auto'):
    check_profile(profile)
    for name, (models, versions) in PROFILES.items():
        if model in models and version in versions and profile in ('auto', name):
            return name
    raise ValueError('PTP model/firmware differs from the selected Fenestra profile')


def nikon_interface(path):
    # Do not guess Z9's product ID from another model. WPD properties and FXP2
    # metadata establish the model/version after opening the single Nikon path.
    return bool(re.search(r'VID_04B0&PID_[0-9A-F]{4}(?:[&#\\]|$)', path.upper()))


def validate_transfer(opcode, params, payload, maximum):
    params = tuple(params)
    if (not isinstance(opcode, int) or len(params) > 5 or
            any(type(n) is not int or not 0 <= n < 1 << 32 for n in params)):
        raise ValueError('PTP parameters')
    if payload is not None:
        if opcode != 0x95e1 or maximum != 0 or not 32 <= len(payload) <= 1056:
            raise ValueError('invalid Fenestra submit')
    elif (opcode not in (0x95e0, 0x95e2, 0x95e3) or
          type(maximum) is not int or not 1 <= maximum <= 1056):
        raise ValueError('invalid Fenestra read')
    return params
