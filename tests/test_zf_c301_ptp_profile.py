"""Read-only Zf PTP entry profile tests."""
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
import zf_c301_ptp_profile as profile


class ZfPtpProfileTests(unittest.TestCase):
    def test_inventory_is_three_entries_and_default_off(self):
        self.assertEqual(set(profile.PTP_ENTRIES), {'lookup', 'session', 'operations'})
        self.assertEqual(profile.PTP_ENTRIES['lookup'][0], 0x407F5000)
        self.assertEqual(profile.PTP_ENTRIES['session'][0], 0x407F39B8)

    def test_wrong_application_is_rejected(self):
        with self.assertRaises((FileNotFoundError, ValueError)):
            profile.inspect_application(ROOT / 'missing-zf-eg')


if __name__ == '__main__':
    unittest.main()
