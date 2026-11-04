"""Paired original font-request mutex calls, independently located per target.

Request IDs 0x420d0032..0x420d003a all carry a font configuration in x0.
The global engine-init request (0x31) and other graphics requests are excluded.
"""
import hashlib
from native_ui_dispatch_sites import call

BASE = 0x40160000
PROFILES = {
    'zr-c110': dict(
        sha256='606d6116dc8e1560fb1608743a18840e0f7ab2617d11bcf02af025405aca1542',
        prefix='fx_c110_font_', lock=0x402f08f8, unlock=0x402f090c,
        line_probe=0x402f9f00,measure=0x408e2050,set_layout=0x408e17dc,
        enter=[0x402f91f0, 0x402f953c, 0x402f9654, 0x402fa504, 0x402fa64c,
               0x402fa7a8, 0x402fa960, 0x402fabac, 0x402fac68]),
    'z9-c532': dict(
        sha256='6380ded18395ce11cb327c766721274c85127d2cf1a1e887ee60e3d156810bdd',
        prefix='fx_z9_font_', lock=0x402e4364, unlock=0x402e4378,
        line_probe=0x402ed94c,measure=0x4088f2a0,set_layout=0x4088ea2c,
        enter=[0x402ecc3c, 0x402ecf88, 0x402ed0a0, 0x402edf50, 0x402ee098,
               0x402ee1f4, 0x402ee3ac, 0x402ee5f8, 0x402ee6b4]),
    'z8-c311': dict(
        sha256='f85e0138e2f5e660ece8560de62e0d12cb795e0e14983e50f20b61dce906c46c',
        prefix='fx_z8_font_', lock=0x402e5550, unlock=0x402e5564,
        line_probe=0x402eeb28, measure=0x4088bdac, set_layout=0x4088c620,
        # C311 probe geometry: the set_layout call follows the measure call
        # at +16 (Z9 places set_layout 16 bytes before the probe).
        line_set_offset=16,
        # C311 request pattern (byte-verified at all 13 sites): lock BL at
        # the enter site, event-register BL (0x402e76b4, bit 2) at +8,
        # event-op BL (0x402e550c on 0xa02e8c668) at +16, event-wait BL
        # (0x402e7684) at +24, unlock BL at +28 - the Z9 +0/+28 geometry.
        enter=[0x402edc90, 0x402ede28, 0x402ee174, 0x402ee28c, 0x402ef13c,
               0x402ef284, 0x402ef3e0, 0x402ef598, 0x402ef7e4, 0x402ef8a0,
               0x402efdc0, 0x402f01ec, 0x402f0248]),
}


def sites(target, version=2):
    p = PROFILES[target]
    leave = p.get('leave_delta', 28)
    result={at + delta: p['prefix'] + name for at in p['enter']
            for delta, name in ((0, 'enter'), (leave, 'leave'))}
    if version>=2:result[p['line_probe']]=p['prefix']+'line_metrics'
    return result


def verify_original(target, data):
    p = PROFILES[target]
    if hashlib.sha256(data).hexdigest() != p['sha256']:
        raise ValueError('native font original identity differs')
    leave = p.get('leave_delta', 28)
    for at in p['enter']:
        if data[at - BASE:at - BASE + 4] != call(at, p['lock']):
            raise ValueError('native font request lock differs')
        lv = at + leave
        if data[lv - BASE:lv - BASE + 4] != call(lv, p['unlock']):
            raise ValueError('native font request unlock differs')
        # The request waits for the completion event before releasing the
        # same mutex: bit 2 is passed to the register/wait pair.
        if data[at - BASE + 4:at - BASE + 8] != bytes.fromhex('e0031f32') or \
                data[at - BASE + 20:at - BASE + 24] != bytes.fromhex('e0031f32'):
            raise ValueError('native font request completion event differs')
    at=p['line_probe']
    set_off = p.get('line_set_offset', -16)
    set_at = at + set_off
    if data[at-BASE:at-BASE+4]!=call(at,p['measure']) or data[set_at-BASE:set_at-BASE+4]!=call(set_at,p['set_layout']):
        raise ValueError('native minimum-line-height probe differs')
    return dict(target=target, original_sha256=p['sha256'], paired_requests=len(p['enter']))
