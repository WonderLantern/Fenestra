"""Zf PTP identity matching is host-only and never opens a device."""
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
from ptp_contract import check_profile, identify


class ZfPtpProfileTests(unittest.TestCase):
    def test_zf_c301_identity(self):
        self.assertEqual(check_profile('zf-c301'), 'zf-c301')
        self.assertEqual(identify('Nikon Zf', '3.01'), 'zf-c301')
        self.assertEqual(identify('Zf', 'C 3.01', 'zf-c301'), 'zf-c301')

    def test_other_profiles_do_not_accept_zf(self):
        with self.assertRaises(ValueError):
            identify('Nikon Zf', '3.01', 'z9-c532')
        with self.assertRaises(ValueError):
            check_profile('zf-c302')


if __name__ == '__main__':
    unittest.main()
