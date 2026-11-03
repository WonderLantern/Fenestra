"""Offline Z8 sysctl-lifecycle pinning tests: no firmware material required."""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
from platform_profiles import PROFILES

# Z8 C3.11 system-event ids: names byte-verified against the pinned table at
# 0x43F907F8 (64 entries x 88 bytes) and re-checked on every core build.
# The Zf reference platform numbers the same lifecycle events differently
# (8/20/24/25); a transposition that copies them would map Z8 mount_complete
# (24) onto the Zf card-remove id.
Z8_SYSCTL_EVENTS = {
    'bs_shutdown': 7,        # eSyscCommandId_BSShutdown
    'startup_complete': 19,  # eSyscCommandId_AppFirstStartComplete
    'card_remove': 23,       # eSyscCommandId_CardRemove
    'mount_complete': 24,    # eSyscCommandId_CardMountComplete
}
# Dispatcher FUN_40d60ec8: entry window = four stack-push STPs; the event id
# is loaded (ldrsw) from message+0x20 at entry+0x18. The same four STPs form
# the Zf reference window (identical prefix sha256) - same vendor codebase.
Z8_SYSCTL_DISPATCH = 0x40d60ec8
Z8_SYSCTL_PREFIX_SHA256 = '90f0a19094d99d64fd16a94f4a89813b33637eea1a3d08c22a65f1f7368de5ec'
Z8_SYSCTL_SPAN_SHA256 = 'cf6a5129e6c7055536e11da441a06f2e644257eb96457be5a196212d168b0db1'


class Z8LifecycleOfflineTests(unittest.TestCase):
    def test_z8_sysctl_binding_group(self):
        s = PROFILES['z8-c311']['sysctl']
        self.assertEqual(s['table'], 0x43F907F8)
        self.assertEqual(s['entries'], 64)
        self.assertEqual(s['stride'], 88)
        self.assertEqual(s['dispatch'], Z8_SYSCTL_DISPATCH)
        self.assertEqual(s['window_bytes'], 16)
        self.assertEqual(s['prefix_sha256'], Z8_SYSCTL_PREFIX_SHA256)
        self.assertEqual(s['span_sha256'], Z8_SYSCTL_SPAN_SHA256)

    def test_z8_sysctl_event_numbers(self):
        s = PROFILES['z8-c311']['sysctl']
        self.assertEqual(s['events'], Z8_SYSCTL_EVENTS)
        self.assertEqual(sorted(s['events'].values()), [7, 19, 23, 24])
        # 24 is Z8 mount_complete; the Zf card-remove id must not leak in.
        self.assertNotEqual(s['events']['card_remove'], 24)

    def test_z8_lifecycle_event_constants_in_header(self):
        header = (ROOT / 'src' / 'platform' / 'z8_c311' / 'lifecycle.h').read_text()
        for name, value in Z8_SYSCTL_EVENTS.items():
            self.assertRegex(
                header, rf'FX_Z8_LIFE_EVENT_{name.upper()}\s*=\s*{value}u\b')

    def test_z8_builder_pins_the_same_dispatch_window(self):
        import build_z8_core
        s = PROFILES['z8-c311']['sysctl']
        self.assertEqual(build_z8_core.SYSTEM_HOOK, s['dispatch'])
        self.assertEqual(build_z8_core.SYSTEM_HOOK + 16, build_z8_core.SYSTEM_CONT)


if __name__ == '__main__':
    unittest.main()
